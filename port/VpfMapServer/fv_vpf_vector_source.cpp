// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fv_vpf_vector_source.h"

#include "fv_vpf_detail.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include "fv_vpf_vdt.h"
#include "vpfdb.h"
#include "vpfrcset.h"
#include "variant.h"

namespace fv {

using vpf_detail::ToStd;
using vpf_detail::Trim;
using vpf_detail::Upper;
using vpf_detail::VariantInt;
using vpf_detail::VariantText;

namespace {

// VPF stores coordinate tuples as (X, Y) = (lon, lat), which is exactly what
// variant.h's structs declare, so the member names can be trusted. (During
// V5a they appeared inverted -- that was the LP64 variable-length-count bug
// fixed in vpfrcset.cpp, which shifted every tuple by one float. If these
// ever look swapped again, suspect a cursor bug, not the field names.)
GeoPoint FromVpf2(const coord2_float_t& c) { return GeoPoint{c.lat, c.lon}; }
GeoPoint FromVpf3(const coord3_float_t& c) { return GeoPoint{c.lat, c.lon}; }
GeoPoint FromVpf2d(const coord2_double_t& c) { return GeoPoint{c.lat, c.lon}; }
GeoPoint FromVpf3d(const coord3_double_t& c) { return GeoPoint{c.lat, c.lon}; }

// Pull a variant's coordinate array out in whichever of the four encodings
// the table used.
void AppendCoords(const VPFVariant& v, std::vector<GeoPoint>* out) {
  const int n = v.m_num_coords;
  if (n <= 0) return;
  out->reserve(out->size() + static_cast<size_t>(n));
  if (v.m_2coord_float_coords != nullptr)
    for (int i = 0; i < n; ++i) out->push_back(FromVpf2(v.m_2coord_float_coords[i]));
  else if (v.m_3coord_float_coords != nullptr)
    for (int i = 0; i < n; ++i) out->push_back(FromVpf3(v.m_3coord_float_coords[i]));
  else if (v.m_2coord_double_coords != nullptr)
    for (int i = 0; i < n; ++i) out->push_back(FromVpf2d(v.m_2coord_double_coords[i]));
  else if (v.m_3coord_double_coords != nullptr)
    for (int i = 0; i < n; ++i) out->push_back(FromVpf3d(v.m_3coord_double_coords[i]));
}

GeoRect BoundsOf(const std::vector<std::vector<GeoPoint>>& parts) {
  GeoRect b{{90.0, 180.0}, {-90.0, -180.0}};
  bool any = false;
  for (const auto& part : parts) {
    for (const GeoPoint& p : part) {
      any = true;
      b.ll.lat = std::min(b.ll.lat, p.lat);
      b.ll.lon = std::min(b.ll.lon, p.lon);
      b.ur.lat = std::max(b.ur.lat, p.lat);
      b.ur.lon = std::max(b.ur.lon, p.lon);
    }
  }
  if (!any) return GeoRect{};
  return b;
}

// Join keys: the foreign keys and row id that wire a feature row to its
// primitive. Never feature attributes, in either the style or the identify
// sense — they say where the geometry is, not what the thing is.
bool IsJoinKeyField(const std::string& name) {
  return name == "id" || name == "tile_id" || name == "edg_id" ||
         name == "end_id" || name == "cnd_id" || name == "fac_id" ||
         name == "txt_id";
}

// Structural columns: the join keys plus f_code, which travels separately as
// the feature's style_key. GeoSym never rules on any of these.
bool IsStructuralField(const std::string& name) {
  return name == "f_code" || IsJoinKeyField(name);
}

// VPF's null sentinels for integer fields (MIL-STD-2407): a short int is null
// at -32768, a long int at -2147483648. VariantText renders them verbatim,
// which is right for the STYLE path (GeoSym's ATTEXP tables see exactly what
// Windows saw) but is noise in a popup — "Vertical Reference Category:
// -32768" tells a user nothing. Identify blanks the DISPLAY only; `raw` still
// carries the sentinel, so nothing is lost. Floats need no equivalent: VPF
// encodes a null float as NaN and VariantText already yields "".
bool IsIntegerNullText(const VPFVariant& v, const std::string& text) {
  if (v.m_type == VPF_INT_SHORT) return text == "-32768";
  if (v.m_type == VPF_INT_LONG) return text == "-2147483648";
  return false;
}

// Seek a recordset to a 1-based VPF row id.
//
// The range check is NOT redundant with set_absolute_position's own: that
// function returns FAILURE for an out-of-range row, but only AFTER an ASSERT
// that MFC compiles out of a Release build and <cassert> does not — so a bad
// id read out of the data aborts a headless debug build where it merely
// failed on Windows. Same quirk class as the CGM parser's unknown-opcode
// ASSERT (ledger row 14o), same policy: never hand it a value it asserts on.
bool SeekRow(VPFRecordset* rs, int one_based_id) {
  if (rs == nullptr || one_based_id < 1 || one_based_id > rs->get_record_count())
    return false;
  return rs->set_absolute_position(one_based_id - 1) == SUCCESS && !rs->is_eof();
}

bool Intersects(const GeoRect& a, const GeoRect& b) {
  return !(a.ur.lat < b.ll.lat || a.ll.lat > b.ur.lat ||
           a.ur.lon < b.ll.lon || a.ll.lon > b.ur.lon);
}

// ---------------------------------------------------------------------------
// AREA topology (V5c). A .AFT row names a FACE; a face is a set of rings, and
// a ring is a loop of directed EDGES linked by the winged-edge fields
// (right_edge/left_edge/right_face/left_face). This is the walk that lived in
// the Windows-only VPFFace/vpfelem: extracted here, geometry only, none of the
// GDI/CRgn/MapProj drawing. The algorithm below is VPFFace::TraverseRing +
// GetPointList reproduced faithfully over an in-memory edge array.
// ---------------------------------------------------------------------------

// One edge of a tile+coverage, indices already converted to 0-based (VPF
// stores them 1-based) exactly as VPFEdge's recordset constructor does.
struct Edge {
  long start_node = -1;  // 0-based index into the tile's connected nodes
  long end_node = -1;
  int right_edge = -1;   // 0-based edge indices (winged-edge links)
  int left_edge = -1;
  int right_face = -1;   // 0-based face ids
  int left_face = -1;
  std::vector<GeoPoint> coords;
};

struct TileTopology {
  std::vector<Edge> edges;  // indexed by 0-based edge id
};

// One entry in a traversed ring: an edge and whether its coordinate run is
// consumed in reverse (topology entered it at its end node).
struct RingEdge {
  int edge = -1;
  bool reversed = false;
};

// Read a winged-edge foreign key (right_edge/left_edge/right_face/left_face)
// as a 0-based id. In DNC these are triplet (K) fields; VPFEdge treated an
// I-type the same way, so both are handled. tile_id/ext_id components are
// ignored: DNC faces are self-contained within a tile, and only the local id
// is used to index the edge array (as in the original).
int WingedId(const VPFVariant* v) {
  if (v == nullptr) return -1;
  if (v->is_int_long()) return static_cast<int>(v->m_int_long) - 1;
  if (v->is_triplet_id()) return static_cast<int>(v->m_triplet_id.id) - 1;
  if (v->is_int_short()) return static_cast<int>(v->m_int_short) - 1;
  return -1;
}

// eDirection from VPFFace.h: outer ring is traversed "clockwise" (0), inner
// rings "counter-clockwise" (1). The value participates in the winged-edge
// decision (the LastNode==-1 seed), so it is carried through verbatim.
enum RingDir { kOuter = 0, kInner = 1 };

// VPFFace::TraverseRing, verbatim. Walks the winged-edge topology from
// `start_edge` back to itself, appending each edge (and its reverse flag) to
// *out. `face` is the 0-based face id. Returns whether the ring has body
// (a real area rather than a bare line) via *body. `edges` is indexed by
// 0-based edge id; out-of-range links abandon the ring (a malformed-data /
// cross-tile guard the GDI original lacked — it would have indexed garbage).
void TraverseRing(const std::vector<Edge>& edges, int face, int start_edge,
                  RingDir dir, std::vector<RingEdge>* out, bool* body) {
  *body = false;
  if (start_edge < 0 || start_edge >= static_cast<int>(edges.size())) return;

  int cur = start_edge;
  long last_node = -1;
  int start_node = -1;
  bool start_edge_reversed = false;
  bool reversed = false;
  int next = -1;

  // Directed edge-uses are unique, so a valid ring visits at most ~2N of them;
  // the cap turns a malformed loop into an abandoned ring instead of a hang.
  const size_t cap = edges.size() * 2 + 16;

  do {
    const Edge& e = edges[cur];

    if (e.left_face == e.right_face) {
      // An edge with the same face on both sides (a dangling spur into the
      // face): keep going the same way until the other side is reached.
      if ((e.end_node != last_node) || (last_node == -1 && dir == kOuter)) {
        next = e.right_edge;
        last_node = e.end_node;
      } else {
        next = e.left_edge;
        last_node = e.start_node;
        if (start_node == -1) start_edge_reversed = true;
      }
    } else if (face == e.left_face) {
      next = e.left_edge;
      last_node = e.start_node;
      *body = true;
      if (start_node == -1) start_edge_reversed = true;
    } else {
      next = e.right_edge;
      last_node = e.end_node;
      *body = true;
    }

    if (start_node == -1) start_node = static_cast<int>(last_node);

    if (next < 0 || next >= static_cast<int>(edges.size())) return;  // guard
    cur = next;
    const Edge& ne = edges[cur];

    // Reverse if this edge's start node is not the node we just crossed, or
    // the loop-edge left-face rule applies (a self-loop edge).
    reversed = (ne.start_node != last_node) ||
               ((ne.left_face != ne.right_face) && (face == ne.left_face));

    out->push_back({cur, reversed});
    if (out->size() > cap) return;  // malformed-data guard (see above)

    // Stop when we re-cross the first edge in the same direction we left it;
    // just re-crossing the start node is not enough (the start edge can be a
    // tangent, entered heading away from the face).
  } while (!(next == start_edge && start_edge_reversed == reversed));
}

// VPFFace::GetPointList, geometry only: concatenate each ring edge's vertices,
// reversing the run when the traversal marked the edge reversed.
void RingPoints(const std::vector<Edge>& edges,
                const std::vector<RingEdge>& ring, std::vector<GeoPoint>* out) {
  for (const RingEdge& re : ring) {
    if (re.edge < 0 || re.edge >= static_cast<int>(edges.size())) continue;
    const std::vector<GeoPoint>& c = edges[re.edge].coords;
    if (re.reversed)
      out->insert(out->end(), c.rbegin(), c.rend());
    else
      out->insert(out->end(), c.begin(), c.end());
  }
}

}  // namespace

// ---------------------------------------------------------------------------

struct VpfVectorSource::Impl {
  std::string root;      // database dir WITH trailing separator
  std::string lib_name;  // e.g. "h1707300"
  std::unique_ptr<VPFLibrary> lib;

  struct Layer {
    std::string coverage;    // "hyd"
    std::string name;        // "hydline"
    std::string table;       // "HYDLINE.LFT"
    std::string prim_field;  // "edg_id" / "end_id" / "cnd_id"
    std::string prim_table;  // "EDG" / "END" / "CND"
    VectorGeometryType type = VectorGeometryType::kLine;
  };
  std::vector<Layer> layers;

  std::map<int, std::string> tile_names;  // tile_id -> directory name

  // R3c: every feature in the library, in scan order, built by the first
  // Query. See the note above VpfVectorSource::Query.
  std::vector<VectorFeature> cache;
  bool cache_built = false;
  bool cache_enabled = true;

  // Primitive tables are reopened constantly while walking a feature table;
  // keep them open, keyed by "<tiledir>/<PRIM>".
  std::map<std::string, std::unique_ptr<VPFRecordset>> prim_cache;

  // Edge topology for area building, loaded once per (coverage, tile) and
  // keyed the same way as prim_cache. A null value is a negative cache entry.
  std::map<std::string, std::unique_ptr<TileTopology>> topo_cache;

  // Identify (§5.3): per-coverage value dictionaries (INT.VDT/CHAR.VDT) and
  // FCA class descriptions, both loaded on first Describe() of that coverage —
  // the render path never touches them.
  std::map<std::string, VpfValueDescriptions> vdt_cache;
  std::map<std::string, std::map<std::string, std::string>> fca_cache;

  const VpfValueDescriptions& Vdt(const std::string& cov) {
    auto it = vdt_cache.find(cov);
    if (it != vdt_cache.end()) return it->second;
    VpfValueDescriptions& v = vdt_cache[cov];
    v.Load(CoverageDir(cov));
    return v;
  }

  // <coverage>/FCA: fclass -> descr ("hydline" -> "Hydrography Line Features").
  const std::map<std::string, std::string>& Fca(const std::string& cov) {
    auto it = fca_cache.find(cov);
    if (it != fca_cache.end()) return it->second;
    std::map<std::string, std::string>& m = fca_cache[cov];
    VPFRecordset rs(CString(CoverageDir(cov).c_str()));
    if (rs.open(CString("FCA")) != SUCCESS) return m;
    if (rs.get_field_info(CString("fclass")) == nullptr ||
        rs.get_field_info(CString("descr")) == nullptr)
      return m;
    rs.move_first();
    while (!rs.is_eof()) {
      VPFVariant* fc = rs.get_field_value(CString("fclass"));
      VPFVariant* de = rs.get_field_value(CString("descr"));
      if (fc != nullptr && de != nullptr) {
        const std::string key = vpf_detail::Lower(Trim(VariantText(*fc)));
        if (!key.empty()) m.emplace(key, Trim(VariantText(*de)));
      }
      rs.move_next();
    }
    return m;
  }

  std::string CoverageDir(const std::string& cov) const {
    return root + lib_name + "\\" + cov + "\\";
  }

  VPFRecordset* PrimitiveTable(const std::string& cov, int tile_id,
                               const std::string& prim) {
    auto tn = tile_names.find(tile_id);
    if (tn == tile_names.end()) return nullptr;
    const std::string key = cov + "/" + tn->second + "/" + prim;
    auto it = prim_cache.find(key);
    if (it != prim_cache.end()) return it->second.get();

    const std::string dir = CoverageDir(cov) + tn->second + "\\";
    auto rs = std::unique_ptr<VPFRecordset>(new VPFRecordset(CString(dir.c_str())));
    if (rs->open(CString(prim.c_str())) != SUCCESS) {
      prim_cache[key] = nullptr;  // negative cache: don't retry per feature
      return nullptr;
    }
    VPFRecordset* raw = rs.get();
    prim_cache[key] = std::move(rs);
    return raw;
  }

  // Load (once) the EDG table of a tile into an in-memory Edge array, applying
  // VPFEdge's exact 1-based->0-based conversions and its add-last-vertex
  // correction (append the end node's coordinate when the edge's coordinate
  // string stops short of it). CND supplies the node coordinates. Returns null
  // if the tile has no EDG table.
  TileTopology* Topology(const std::string& cov, int tile_id);

  // Turn an area feature's face into rings: rings[0] = outer boundary,
  // rings[1..] = holes. Reproduces the VPFFace constructor's FAC->RNG walk.
  bool BuildFace(const std::string& cov, int tile_id, int fac_id,
                 std::vector<std::vector<GeoPoint>>* rings);

  void LoadTileNames();
  void DiscoverLayers();
};

// ---------------------------------------------------------------------------

TileTopology* VpfVectorSource::Impl::Topology(const std::string& cov,
                                              int tile_id) {
  auto tn = tile_names.find(tile_id);
  if (tn == tile_names.end()) return nullptr;
  const std::string key = cov + "/" + tn->second + "/EDG";
  auto it = topo_cache.find(key);
  if (it != topo_cache.end()) return it->second.get();

  const std::string dir = CoverageDir(cov) + tn->second + "\\";

  // Connected-node coordinates (CND), indexed 0-based, for the add-last-vertex
  // correction. Absent CND just means no correction is applied.
  std::vector<GeoPoint> nodes;
  {
    VPFRecordset cnd(CString(dir.c_str()));
    if (cnd.open(CString("CND")) == SUCCESS) {
      nodes.reserve(static_cast<size_t>(cnd.get_record_count()));
      cnd.move_first();
      while (!cnd.is_eof()) {
        VPFVariant* c = cnd.get_field_value(CString("coordinate"));
        std::vector<GeoPoint> one;
        if (c != nullptr) AppendCoords(*c, &one);
        nodes.push_back(one.empty() ? GeoPoint{0.0, 0.0} : one.front());
        cnd.move_next();
      }
    }
  }

  VPFRecordset edg(CString(dir.c_str()));
  if (edg.open(CString("EDG")) != SUCCESS) {
    topo_cache[key] = nullptr;
    return nullptr;
  }

  auto topo = std::unique_ptr<TileTopology>(new TileTopology);
  topo->edges.reserve(static_cast<size_t>(edg.get_record_count()));
  edg.move_first();
  while (!edg.is_eof()) {
    Edge e;
    if (VPFVariant* v = edg.get_field_value(CString("start_node")))
      e.start_node = VariantInt(*v) - 1;
    if (VPFVariant* v = edg.get_field_value(CString("end_node")))
      e.end_node = VariantInt(*v) - 1;
    if (VPFVariant* v = edg.get_field_value(CString("coordinates")))
      AppendCoords(*v, &e.coords);
    e.right_edge = WingedId(edg.get_field_value(CString("right_edge")));
    e.left_edge = WingedId(edg.get_field_value(CString("left_edge")));
    e.right_face = WingedId(edg.get_field_value(CString("right_face")));
    e.left_face = WingedId(edg.get_field_value(CString("left_face")));

    // VPFEdge: if the edge's last vertex is not exactly the end node, append
    // the node so the ring closes on it (some edges are not encoded to reach
    // their node). Exact float compare, matching the original.
    if (!e.coords.empty() && e.end_node >= 0 &&
        e.end_node < static_cast<long>(nodes.size())) {
      const GeoPoint& node = nodes[static_cast<size_t>(e.end_node)];
      const GeoPoint& last = e.coords.back();
      if (node.lat != last.lat || node.lon != last.lon) e.coords.push_back(node);
    }
    topo->edges.push_back(std::move(e));
    edg.move_next();
  }

  TileTopology* raw = topo.get();
  topo_cache[key] = std::move(topo);
  return raw;
}

bool VpfVectorSource::Impl::BuildFace(const std::string& cov, int tile_id,
                                      int fac_id,
                                      std::vector<std::vector<GeoPoint>>* rings) {
  TileTopology* topo = Topology(cov, tile_id);
  if (topo == nullptr) return false;
  const std::vector<Edge>& edges = topo->edges;

  // FAC row (fac_id, 1-based) -> ring_ptr, a 1-based row into RNG.
  VPFRecordset* fac = PrimitiveTable(cov, tile_id, "FAC");
  if (fac == nullptr) return false;
  if (!SeekRow(fac, fac_id)) return false;
  VPFVariant* rp = fac->get_field_value(CString("ring_ptr"));
  if (rp == nullptr) return false;
  const int ring_id = VariantInt(*rp);

  VPFRecordset* rng = PrimitiveTable(cov, tile_id, "RNG");
  if (rng == nullptr) return false;
  if (!SeekRow(rng, ring_id)) return false;

  VPFVariant* fid = rng->get_field_value(CString("face_id"));
  VPFVariant* se = rng->get_field_value(CString("start_edge"));
  if (fid == nullptr || se == nullptr) return false;
  const int face_id = VariantInt(*fid);  // == fac_id in well-formed data
  const int start_edge = VariantInt(*se) - 1;

  // Outer ring (always kept, body or not — matches VPFFace).
  std::vector<RingEdge> outer;
  bool body = false;
  TraverseRing(edges, face_id - 1, start_edge, kOuter, &outer, &body);

  // Bit-faithful: VPFFace discards faces whose outer ring exceeds 1500 edges
  // (a data-problem heuristic — CRgn's 64K limit on Windows). We drop the
  // whole feature, which is what the discarded face drew: nothing.
  if (outer.size() > 1500) return false;

  std::vector<GeoPoint> outer_ring;
  RingPoints(edges, outer, &outer_ring);
  if (outer_ring.empty()) return false;
  rings->push_back(std::move(outer_ring));

  // Subsequent RNG rows with the same face_id are inner rings (holes); only
  // rings with body are kept (a bare line has no area to subtract).
  rng->move_next();
  while (!rng->is_eof()) {
    VPFVariant* rf = rng->get_field_value(CString("face_id"));
    if (rf == nullptr || VariantInt(*rf) != face_id) break;
    VPFVariant* rs2 = rng->get_field_value(CString("start_edge"));
    if (rs2 != nullptr) {
      std::vector<RingEdge> inner;
      bool inner_body = false;
      TraverseRing(edges, face_id - 1, VariantInt(*rs2) - 1, kInner, &inner,
                   &inner_body);
      if (inner_body) {
        std::vector<GeoPoint> inner_ring;
        RingPoints(edges, inner, &inner_ring);
        if (!inner_ring.empty()) rings->push_back(std::move(inner_ring));
      }
    }
    rng->move_next();
  }
  return true;
}

// tileref/TILEREF.AFT maps tile ids to the directory names the primitives
// live under. NOTE we deliberately do NOT read the tile BOUNDS here: a
// TILEREF row points at a face, and resolving a face to a rectangle needs the
// topology walk that is still Windows-only (V5b). Feature bounds are computed
// from the geometry instead, which is what the query filter uses.
void VpfVectorSource::Impl::LoadTileNames() {
  const std::string dir = root + lib_name + "\\tileref\\";
  VPFRecordset rs(CString(dir.c_str()));
  if (rs.open(CString("TILEREF.AFT")) != SUCCESS) return;

  while (!rs.is_eof()) {
    VPFVariant* id = rs.get_field_value(CString("id"));
    VPFVariant* nm = rs.get_field_value(CString("tile_name"));
    if (id != nullptr && nm != nullptr) {
      const std::string name = Trim(ToStd(nm->m_text));
      if (!name.empty()) tile_names[VariantInt(*id)] = name;
    }
    rs.move_next();
  }
}

void VpfVectorSource::Impl::DiscoverLayers() {
  const CStringList* covs = lib->get_coverage_names();
  if (covs == nullptr) return;

  POSITION pos = covs->GetHeadPosition();
  while (pos != nullptr) {
    const std::string cov = Trim(ToStd(covs->GetNext(pos)));
    if (cov.empty() || cov == "tileref" || cov == "libref") continue;

    VPFCoverage* c = lib->open_coverage(CString(cov.c_str()));
    if (c == nullptr) continue;
    const std::vector<VPFFeatureClass*>* fcs = c->get_feature_class_list();
    if (fcs == nullptr) continue;

    for (VPFFeatureClass* fc : *fcs) {
      if (fc == nullptr) continue;
      const std::string name = Trim(ToStd(fc->GetName()));
      if (name.empty()) continue;

      Layer L;
      L.coverage = cov;
      L.name = name;
      // delineation: 1 = point, 2 = line, 3 = area (pinned by the V1 tests).
      switch (fc->delineation()) {
        case 2:
          L.type = VectorGeometryType::kLine;
          L.table = Upper(name) + ".LFT";
          L.prim_field = "edg_id";
          L.prim_table = "EDG";
          break;
        case 1:
          L.type = VectorGeometryType::kPoint;
          L.table = Upper(name) + ".PFT";
          // Point features reference either an entity node (END) or a
          // connected node (CND); which one is decided per table below.
          L.prim_field = "end_id";
          L.prim_table = "END";
          break;
        case 3: {
          // Area (V5c): the feature table row points at a FACE, resolved to
          // rings via the FAC/RNG/EDG topology walk in BuildFace.
          L.type = VectorGeometryType::kArea;
          L.table = Upper(name) + ".AFT";
          L.prim_field = "fac_id";
          L.prim_table = "FAC";
          // A drawable area feature class carries a FACC. DNC's data-quality
          // coverage (dqyarea) is chart metadata — no f_code, no symbology —
          // so it is not a vector feature layer. Skip any area .AFT with no
          // f_code column, keeping the "every feature has a style key"
          // invariant the point/line classes already satisfy.
          VPFRecordset probe(CString(CoverageDir(cov).c_str()));
          if (probe.open(CString(L.table.c_str())) != SUCCESS ||
              probe.get_field_info(CString("f_code")) == nullptr)
            continue;
          break;
        }
        default:
          continue;  // anything else is not drawable geometry
      }
      layers.push_back(L);
    }
  }
}

// ---------------------------------------------------------------------------

VpfVectorSource::VpfVectorSource() : impl_(new Impl) {}
VpfVectorSource::~VpfVectorSource() = default;

Status VpfVectorSource::Open(const std::string& path) {
  // A fresh Impl drops the parsed-feature cache with everything else, which
  // is what re-opening onto another library must do. The cache SETTING is a
  // property of the caller, not of the library, so it is carried over.
  const bool keep_cache_enabled = impl_ ? impl_->cache_enabled : true;
  impl_.reset(new Impl);
  impl_->cache_enabled = keep_cache_enabled;

  std::string p = path;
  while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
  const size_t slash = p.find_last_of("/\\");
  if (slash == std::string::npos)
    return Status::Error(kInvalidArg, "expected <database>/<library>: " + path);

  impl_->root = p.substr(0, slash + 1);
  impl_->lib_name = p.substr(slash + 1);

  impl_->lib.reset(new VPFLibrary(CString(impl_->root.c_str()),
                                  CString(impl_->lib_name.c_str())));
  if (!impl_->lib->is_open()) {
    impl_->lib.reset();
    return Status::Error(kNotFound, "not a VPF library: " + path);
  }

  impl_->LoadTileNames();
  impl_->DiscoverLayers();
  if (impl_->layers.empty())
    return Status::Error(kUnsupported,
                         "no point/line feature classes in " + path);
  return Status::Ok();
}

bool VpfVectorSource::IsOpen() const {
  return impl_ && impl_->lib && impl_->lib->is_open();
}

std::vector<std::string> VpfVectorSource::Layers() const {
  std::vector<std::string> out;
  if (impl_) for (const auto& L : impl_->layers) out.push_back(L.name);
  return out;
}

GeoRect VpfVectorSource::Bounds() const {
  // Computed from geometry (see LoadTileNames' note). Callers that need it
  // before querying pay one full scan; the catalog (14m) already stores
  // per-tile coverage for culling, so this stays a convenience.
  GeoRect b{{90.0, 180.0}, {-90.0, -180.0}};
  if (!impl_) return GeoRect{};
  std::vector<VectorFeature> all;
  VectorQuery q;
  const_cast<VpfVectorSource*>(this)->Query(q, &all);
  if (all.empty()) return GeoRect{};
  for (const auto& f : all) {
    b.ll.lat = std::min(b.ll.lat, f.bounds.ll.lat);
    b.ll.lon = std::min(b.ll.lon, f.bounds.ll.lon);
    b.ur.lat = std::max(b.ur.lat, f.bounds.ur.lat);
    b.ur.lon = std::max(b.ur.lon, f.bounds.ur.lon);
  }
  return b;
}

// R3c. Answering from the parsed cache, which is built on the first Query and
// never rebuilt: a VPF library is read-only data and this source's answer
// depends only on the query BOX (DNC's scale is the choice of library, so
// VectorQuery::scale_denominator is not consulted anywhere in this file).
//
// The cache is opt-out rather than budgeted. One library's features are ~1.2x
// its size on disk — the Nantucket harbour library is 3.1 MB on disk and
// 3.7 MB parsed — and a source holds exactly one library, so a cap would add a
// re-scan cliff to save an amount of memory nobody is short of. A caller that
// disagrees turns it off and gets the pre-R3c full scan per query.
Status VpfVectorSource::Query(const VectorQuery& q,
                              std::vector<VectorFeature>* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "null out");
  if (!IsOpen()) return Status::Error(kNotFound, "source not open");

  if (!impl_->cache_enabled) {
    std::vector<VectorFeature> all;
    const Status s = ScanAll(&all);
    if (!s.ok()) return s;
    AppendMatching(all, q, out);
    return Status::Ok();
  }

  if (!impl_->cache_built) {
    const Status s = ScanAll(&impl_->cache);
    if (!s.ok()) {
      impl_->cache.clear();
      return s;
    }
    impl_->cache_built = true;
  }
  AppendMatching(impl_->cache, q, out);
  return Status::Ok();
}

void VpfVectorSource::SetFeatureCacheEnabled(bool on) {
  if (!impl_) return;
  impl_->cache_enabled = on;
  if (!on) {
    impl_->cache.clear();
    impl_->cache.shrink_to_fit();
    impl_->cache_built = false;
  }
}

bool VpfVectorSource::feature_cache_enabled() const {
  return impl_ && impl_->cache_enabled;
}

size_t VpfVectorSource::cached_features() const {
  return impl_ ? impl_->cache.size() : 0;
}

// The two filters ScanAll no longer applies, in the order the scan applied
// them: a feature is tested against the box, and max_features counts what is
// already in `out` (Query appends — the seam's contract — so a caller's own
// earlier hits count toward its limit exactly as they did before).
void VpfVectorSource::AppendMatching(const std::vector<VectorFeature>& src,
                                     const VectorQuery& q,
                                     std::vector<VectorFeature>* out) {
  for (const VectorFeature& f : src) {
    if (q.max_features != 0 && out->size() >= q.max_features) return;
    if (Intersects(f.bounds, q.area)) out->push_back(f);
  }
}

// The whole library, materialized once (R3c). This IS the pre-R3c Query with
// its two filters removed: the spatial test and the max_features cut now
// happen in Query, over what this produced, and the ORDER is unchanged — so
// "the first N features that meet the box, in layer-then-row order" means the
// same thing it always did.
//
// It walks every row of every feature table, and it did so on EVERY Query
// before R3c: a viewport returning 5 features cost the same 9.4 ms as one
// returning 5,037, because the cost never depended on the answer. That is the
// measurement this split exists for.
Status VpfVectorSource::ScanAll(std::vector<VectorFeature>* out) {
  const VectorQuery q;  // unfiltered; kept named so the walk below reads on

  // Every non-structural column travels with the feature: the GeoSym rule
  // engine dispatches on them (exs, nam, dof, hdp, …) and a hit-test wants
  // them verbatim. Empty values are KEPT — see VariantText: GeoSym
  // distinguishes a null attribute from an absent one.
  auto collect_attributes = [](VPFRecordset& rs, VectorFeature* f) {
    const int nfields = rs.get_field_count();
    f->attributes.reserve(static_cast<size_t>(nfields));
    for (int fi = 0; fi < nfields; ++fi) {
      const VPFFieldInfo* info = rs.get_field_info(fi);
      if (info == nullptr) continue;
      const std::string name = Trim(ToStd(info->m_name));
      if (name.empty() || IsStructuralField(name)) continue;
      VPFVariant* val = rs.get_field_value(CString(name.c_str()));
      if (val == nullptr) continue;
      f->attributes.push_back({name, VariantText(*val)});
    }
  };

  for (size_t li = 0; li < impl_->layers.size(); ++li) {
    const Impl::Layer& L = impl_->layers[li];
    const int32_t layer_index = static_cast<int32_t>(li);
    if (q.max_features != 0 && out->size() >= q.max_features) break;

    VPFRecordset rs(CString(impl_->CoverageDir(L.coverage).c_str()));
    if (rs.open(CString(L.table.c_str())) != SUCCESS) continue;

    // Area features: each .AFT row names a face, which BuildFace resolves to
    // rings. This path is distinct from the point/line single-primitive read.
    if (L.type == VectorGeometryType::kArea) {
      // Some DNC area tables are COMPLEX features (e.g. lim/LIMBNDYA — maritime
      // limit boundaries) with no simple tile_id/fac_id face reference; the
      // single-face walk cannot build them, and probing each row for the
      // absent columns floods the log. Skip such tables up front (quietly:
      // get_field_info does not log, get_field_value does).
      if (rs.get_field_info(CString("tile_id")) == nullptr ||
          rs.get_field_info(CString("fac_id")) == nullptr)
        continue;

      while (!rs.is_eof()) {
        if (q.max_features != 0 && out->size() >= q.max_features) break;

        VPFVariant* v_tile = rs.get_field_value(CString("tile_id"));
        VPFVariant* v_fac = rs.get_field_value(CString("fac_id"));
        VPFVariant* v_fc = rs.get_field_value(CString("f_code"));
        VPFVariant* v_id = rs.get_field_value(CString("id"));
        if (v_tile == nullptr || v_fac == nullptr) { rs.move_next(); continue; }

        const int tile_id = VariantInt(*v_tile);
        const int fac_id = VariantInt(*v_fac);
        // Face 0 is illegal and face 1 is the universe face (VPFFace asserts
        // FaceID > 1); neither is a drawable area.
        if (fac_id <= 1) { rs.move_next(); continue; }

        std::vector<std::vector<GeoPoint>> rings;
        if (!impl_->BuildFace(L.coverage, tile_id, fac_id, &rings) ||
            rings.empty()) {
          rs.move_next();
          continue;
        }

        VectorFeature f;
        f.type = VectorGeometryType::kArea;
        f.layer = L.name;
        f.ref.layer = layer_index;
        f.ref.tile = tile_id;
        f.ref.feature = (v_id != nullptr) ? VariantInt(*v_id) : 0;
        if (v_fc != nullptr) f.style_key = Trim(ToStd(v_fc->m_text));
        collect_attributes(rs, &f);
        f.parts = std::move(rings);
        f.bounds = BoundsOf(f.parts);

        if (Intersects(f.bounds, q.area)) out->push_back(std::move(f));
        rs.move_next();
      }
      continue;
    }

    // Point tables reference END or CND depending on the product; pick
    // whichever column this table actually declares.
    std::string prim_field = L.prim_field, prim_table = L.prim_table;
    if (L.type == VectorGeometryType::kPoint) {
      if (rs.get_field_info(CString("end_id")) != nullptr) {
        prim_field = "end_id"; prim_table = "END";
      } else if (rs.get_field_info(CString("cnd_id")) != nullptr) {
        prim_field = "cnd_id"; prim_table = "CND";
      } else {
        continue;
      }
    }

    // Complex line features (e.g. lim/LIMBNDYL, lim/MARITIML) carry no simple
    // tile_id/edg_id primitive reference; skip up front rather than log a miss
    // per row (get_field_info is quiet; get_field_value is not).
    if (rs.get_field_info(CString("tile_id")) == nullptr ||
        rs.get_field_info(CString(prim_field.c_str())) == nullptr)
      continue;

    while (!rs.is_eof()) {
      if (q.max_features != 0 && out->size() >= q.max_features) break;

      VPFVariant* v_tile = rs.get_field_value(CString("tile_id"));
      VPFVariant* v_prim = rs.get_field_value(CString(prim_field.c_str()));
      VPFVariant* v_fc = rs.get_field_value(CString("f_code"));
      VPFVariant* v_id = rs.get_field_value(CString("id"));
      if (v_tile == nullptr || v_prim == nullptr) { rs.move_next(); continue; }

      const int tile_id = VariantInt(*v_tile);
      const int prim_id = VariantInt(*v_prim);
      VPFRecordset* prim =
          impl_->PrimitiveTable(L.coverage, tile_id, prim_table);
      if (prim == nullptr || prim_id <= 0) { rs.move_next(); continue; }

      // VPF row ids are 1-based and dense, so id-1 is the row index.
      if (!SeekRow(prim, prim_id)) { rs.move_next(); continue; }
      // Edges carry "coordinates" (a string of tuples); entity/connected
      // node tables carry a single "coordinate". Probe the schema with
      // get_field_info first (which is quiet) — get_field_value logs a
      // "does not exist" line per miss, and a point table misses "coordinates"
      // on every one of its thousands of rows, flooding an interactive caller.
      VPFVariant* coords = nullptr;
      if (prim->get_field_info(CString("coordinates")) != nullptr)
        coords = prim->get_field_value(CString("coordinates"));
      else if (prim->get_field_info(CString("coordinate")) != nullptr)
        coords = prim->get_field_value(CString("coordinate"));
      if (coords == nullptr) { rs.move_next(); continue; }

      VectorFeature f;
      f.type = L.type;
      f.layer = L.name;
      f.ref.layer = layer_index;
      f.ref.tile = tile_id;
      f.ref.feature = (v_id != nullptr) ? VariantInt(*v_id) : 0;
      if (v_fc != nullptr) f.style_key = Trim(ToStd(v_fc->m_text));

      collect_attributes(rs, &f);

      std::vector<GeoPoint> part;
      AppendCoords(*coords, &part);
      if (part.empty()) { rs.move_next(); continue; }
      f.parts.push_back(std::move(part));
      f.bounds = BoundsOf(f.parts);

      if (Intersects(f.bounds, q.area)) out->push_back(std::move(f));
      rs.move_next();
    }
  }
  return Status::Ok();
}

// ---------------------------------------------------------------------------
// Identify (plan §5.3). Re-reads ONE row and decodes it for a human: the
// column's own schema description (VPFFieldInfo::m_desc, straight out of the
// table header), the coded value expanded through the coverage's INT.VDT /
// CHAR.VDT, and the class description from the FCA. Nothing here is on the
// render path — the dictionaries load on the first Describe of a coverage.
// ---------------------------------------------------------------------------

Status VpfVectorSource::Describe(const FeatureRef& ref,
                                 FeatureDescription* out) {
  if (out == nullptr) return Status::Error(kInvalidArg, "null out");
  if (!IsOpen()) return Status::Error(kNotFound, "source not open");
  if (ref.layer < 0 || static_cast<size_t>(ref.layer) >= impl_->layers.size())
    return Status::Error(kInvalidArg, "feature ref names no layer");

  const Impl::Layer& L = impl_->layers[static_cast<size_t>(ref.layer)];

  VPFRecordset rs(CString(impl_->CoverageDir(L.coverage).c_str()));
  if (rs.open(CString(L.table.c_str())) != SUCCESS)
    return Status::Error(kIoError, "cannot open " + L.table);

  // VPF row ids are 1-based and dense, so id-1 IS the row — but verify, and
  // fall back to a scan if a table ever breaks that (Query trusts the same
  // assumption for primitives, where it is guaranteed by the spec; feature
  // tables are only conventionally dense).
  bool found = false;
  if (SeekRow(&rs, ref.feature)) {
    VPFVariant* id = rs.get_field_value(CString("id"));
    found = (id != nullptr && VariantInt(*id) == ref.feature);
  }
  if (!found) {
    rs.move_first();
    while (!rs.is_eof()) {
      VPFVariant* id = rs.get_field_value(CString("id"));
      if (id != nullptr && VariantInt(*id) == ref.feature) { found = true; break; }
      rs.move_next();
    }
  }
  if (!found)
    return Status::Error(kNotFound, "no such feature in " + L.table);

  const VpfValueDescriptions& vdt = impl_->Vdt(L.coverage);

  *out = FeatureDescription();
  out->ref = ref;
  out->layer_name = L.name;
  {
    const auto& fca = impl_->Fca(L.coverage);
    auto it = fca.find(vpf_detail::Lower(L.name));
    if (it != fca.end()) out->class_name = it->second;
  }

  int row_tile = ref.tile;
  const int nfields = rs.get_field_count();
  out->attributes.reserve(static_cast<size_t>(nfields));
  for (int fi = 0; fi < nfields; ++fi) {
    const VPFFieldInfo* info = rs.get_field_info(fi);
    if (info == nullptr) continue;
    const std::string name = Trim(ToStd(info->m_name));
    if (name.empty()) continue;
    VPFVariant* val = rs.get_field_value(CString(name.c_str()));
    if (val == nullptr) continue;
    if (name == "tile_id") { row_tile = VariantInt(*val); continue; }
    // Join keys are plumbing; f_code IS shown (it is the FACC, the single most
    // useful thing about a DNC feature) and doubles as the title below.
    if (IsJoinKeyField(name)) continue;

    FeatureAttribute a;
    a.code = name;
    a.name = Trim(ToStd(info->m_desc));
    a.raw = VariantText(*val);
    const std::string decoded = vdt.Lookup(L.table, name, a.raw);
    if (!decoded.empty())
      a.display = decoded;
    else if (!IsIntegerNullText(*val, a.raw))
      a.display = a.raw;  // no dictionary entry: show the value itself
    // else: a null integer displays as nothing (raw keeps the sentinel).
    if (name == "f_code" && !decoded.empty()) out->title = decoded;
    out->attributes.push_back(std::move(a));
  }

  if (out->title.empty()) {
    // No FACC description: fall back to the raw FACC, then the class name, so
    // a popup always has a heading.
    for (const FeatureAttribute& a : out->attributes)
      if (a.code == "f_code" && !a.raw.empty()) { out->title = a.raw; break; }
  }
  if (out->title.empty()) out->title = out->class_name;
  if (out->title.empty()) out->title = L.name;

  out->source_note = impl_->lib_name + " " + L.coverage + " " + L.table;
  auto tn = impl_->tile_names.find(row_tile);
  if (tn != impl_->tile_names.end())
    out->source_note += " tile " + tn->second;
  return Status::Ok();
}

}  // namespace fv
