// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_s57.h — IHO S-57 ENC cell reader (ENC phase E1, plan section 7).
//
// Reads a NOAA/IHO S-57 base cell (`*.000`) into features with attributes and
// assembled geometry, over the generic ISO 8211 reader in fv_iso8211.h. Also
// reads an exchange set's CATALOG.031 (itself ISO 8211) and enumerates cells on
// disk. Product-neutral in the sense that matters here: no symbology, no canvas
// — the FvKit `IVectorSource` adapter and S-52 styling are later phases (E3+),
// and this header is what they will sit on.
//
// BASE EDITION ONLY. `.001`, `.002`, … update files are NOT applied (phase E5).
// A cell with unapplied updates on disk is STALE, which is the worst failure
// mode a chart has, so `Open` reports them in `unapplied_updates()`, exposes
// `StalenessWarning()`, and prints that warning to stderr. Callers that must
// not show stale charts should refuse to draw when it is non-empty.
//
// Coordinates are WGS-84 decimal degrees (D2): S-57 stores them as integers
// scaled by the cell's COMF, and soundings scaled by SOMF. HDAT/VDAT/SDAT are
// carried through in the dataset info but no datum shift is applied — NOAA ENC
// is already WGS-84 (HDAT 2).
//
// Object classes (OBJL) and attributes (ATTL) stay NUMERIC here. Their acronyms
// and names live in S-57 Appendix A, which is not in the data and is not
// guessed at: E2 brings the catalogue alongside the S-52 presentation library,
// and `S57Cell` gains a lookup then. Numbers are visible and correct; invented
// acronyms would be neither. That gap also costs the meta/cartographic/geo
// record split (see S57ParseCounts) — both are one catalogue away.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "fv_iso8211.h"
#include "fvkit/geo.h"

namespace fv {

// FRID.PRIM — the feature's geometric primitive.
enum class S57Primitive {
  kPoint = 1,
  kLine = 2,
  kArea = 3,
  kNone = 255,  // "meta" features: M_COVR, M_QUAL, C_AGGR and friends
};

// One attribute of a feature, verbatim from ATTF (national = NATF).
struct S57Attribute {
  int code = 0;        // ATTL, an S-57 Appendix A attribute code
  std::string value;   // ATVL as stored; empty means the null/unknown value
  bool national = false;
};

struct S57Vertex {
  double lat = 0.0;
  double lon = 0.0;
  double depth = 0.0;      // metres, SOMF-scaled; only 3-D (sounding) vertices
  bool has_depth = false;
};

// Assembled geometry. A point feature has one part (a sounding array is one
// part with many vertices); a line has one part per connected run; an area has
// one part per ring, with `part_is_hole` marking the interior ones.
struct S57Geometry {
  std::vector<std::vector<S57Vertex>> parts;
  std::vector<bool> part_is_hole;
};

// FOID — the world-unique feature object identifier.
struct S57ObjectId {
  int agency = 0;            // AGEN
  uint32_t feature_id = 0;   // FIDN
  int subdivision = 0;       // FIDS
};

// FFPT — a pointer to another feature (masters/slaves of a collection).
struct S57FeatureRelation {
  S57ObjectId target;   // LNAM, decoded
  int relation = 0;     // RIND: 1 master, 2 slave, 3 peer
  std::string comment;
};

struct S57Feature {
  int32_t record_id = 0;  // FRID.RCID, unique within the cell
  S57Primitive primitive = S57Primitive::kNone;
  int object_class = 0;  // OBJL (numeric; see the header note)
  int group = 0;         // GRUP: 1 skin-of-the-earth, 2 everything else
  int version = 0;       // RVER
  S57ObjectId object_id;
  std::vector<S57Attribute> attributes;
  std::vector<S57FeatureRelation> relations;
  S57Geometry geometry;
  GeoRect bounds;

  // False when a referenced spatial record was missing or a ring did not close
  // — the geometry is still returned, but it is not the whole feature.
  bool geometry_complete = true;

  // nullptr when the attribute is absent.
  const S57Attribute* FindAttribute(int code) const;
};

struct S57DatasetInfo {
  // DSID
  std::string name;               // DSNM, e.g. "US5CHSDC.000"
  std::string edition;            // EDTN
  std::string update_number;      // UPDN — "0" for a base cell
  std::string update_date;        // UADT
  std::string issue_date;         // ISDT
  std::string standard_edition;   // STED, e.g. "03.1"
  std::string product_edition;    // PRED
  std::string comment;            // COMT
  int producer_agency = 0;        // AGEN (550 = NOAA)
  int intended_usage = 0;         // INTU, the usage band (5 = harbour)

  // DSSI — declared record counts, used to verify the parse.
  int data_structure = 0;         // DSTR: 2 = chain-node (what ENC uses)
  int attribute_lexical_level = 0;  // AALL
  int national_lexical_level = 0;   // NALL: 2 would mean UCS-2 national text
  int declared_meta_records = 0;      // NOMR
  int declared_cartographic_records = 0;  // NOCR
  int declared_geo_records = 0;       // NOGR
  int declared_collection_records = 0;  // NOLR
  int declared_isolated_nodes = 0;    // NOIN
  int declared_connected_nodes = 0;   // NOCN
  int declared_edges = 0;             // NOED
  int declared_faces = 0;             // NOFA

  // DSPM
  int32_t compilation_scale = 0;  // CSCL, e.g. 12000
  double coordinate_multiplier = 1.0;  // COMF
  double sounding_multiplier = 1.0;    // SOMF
  int horizontal_datum = 0;  // HDAT
  int vertical_datum = 0;    // VDAT
  int sounding_datum = 0;    // SDAT
  int depth_units = 0;       // DUNI (1 = metres)
  int height_units = 0;      // HUNI
  int position_units = 0;    // PUNI
  int coordinate_units = 0;  // COUN (1 = lat/lon)
};

// Counts of what was actually parsed, to compare against the DSSI declarations.
//
// DSSI splits feature records into meta / cartographic / geo / collection, but
// that split is a property of the OBJECT CLASS (S-57 Appendix A: M_* are meta,
// C_* are collections), not of anything in the record — so with the catalogue
// absent (see the header note) only the total and the collection count can be
// checked here. Collection objects are the ones with no geometric primitive,
// which in the NOAA cells matches DSSI's NOLR exactly.
struct S57ParseCounts {
  int feature_records = 0;     // all FRID records: NOMR + NOCR + NOGR + NOLR
  int collection_records = 0;  // features with PRIM = kNone; compare to NOLR
  int isolated_nodes = 0;      // VI
  int connected_nodes = 0;     // VC
  int edges = 0;               // VE
  int faces = 0;               // VF (ENC chain-node data has none)
};

class S57Cell {
 public:
  // Reads a base cell. Non-`.000` paths are accepted (an update file opens as
  // its own record set) but only `.000` gets the update-file scan.
  Status Open(const std::string& path);
  bool is_open() const { return is_open_; }

  const std::string& path() const { return path_; }
  const S57DatasetInfo& info() const { return info_; }
  const S57ParseCounts& counts() const { return counts_; }
  const std::vector<S57Feature>& features() const { return features_; }
  // Union of every feature's geometry. A cell with no geometry at all leaves
  // this inverted (ll north/east of ur), which `Intersects` reports as empty.
  GeoRect bounds() const { return bounds_; }

  // Update files found next to the cell and NOT applied, in sequence order.
  const std::vector<std::string>& unapplied_updates() const { return updates_; }
  // Empty when the cell is current; otherwise a sentence naming what is missing.
  std::string StalenessWarning() const;

 private:
  struct Edge {
    uint64_t begin = 0;  // spatial-record key of the begin node (VRPT TOPI 1)
    uint64_t end = 0;    // ... and the end node (TOPI 2)
    std::vector<S57Vertex> interior;
  };

  Status ReadRecords();
  // Builds `feature`'s geometry from the FSPT pointers of its record.
  void AssembleGeometry(const iso8211::Record& record, S57Feature* feature) const;
  // Vertices of an edge, begin node first (reversed when `reverse`). False when
  // the edge itself is missing; `*complete` is cleared when the edge is there
  // but one of its end nodes is not, so a short run is never silently short.
  bool EdgePoints(uint64_t key, bool reverse, std::vector<S57Vertex>* out,
                  bool* complete) const;

  bool is_open_ = false;
  std::string path_;
  S57DatasetInfo info_;
  S57ParseCounts counts_;
  std::vector<S57Feature> features_;
  // Spatial records by (RCNM, RCID) key — looked up per edge, never iterated in
  // order, so hashing beats a tree here.
  std::unordered_map<uint64_t, std::vector<S57Vertex>> nodes_;
  std::unordered_map<uint64_t, Edge> edges_;
  std::vector<std::string> updates_;
  GeoRect bounds_{{90.0, 180.0}, {-90.0, -180.0}};
};

// ---------------------------------------------------------------------------
// Exchange-set level
// ---------------------------------------------------------------------------

// One CATD record of a CATALOG.031: a cell's bounds and long name without
// opening the cell.
struct S57CatalogEntry {
  int32_t record_id = 0;
  std::string file;            // FILE, e.g. "ENC_ROOT/US5CHSEC/US5CHSEC.000"
  std::string long_file_name;  // LFIL
  std::string volume;          // VOLM
  std::string implementation;  // IMPL: "BIN" / "ASC"
  std::string crc;             // CRCS
  std::string comment;         // COMT
  GeoRect bounds;
  bool has_bounds = false;  // SLAT/WLON/NLAT/ELON present (non-cell rows omit them)
};

Status ReadS57Catalog(const std::string& path, std::vector<S57CatalogEntry>* out);

// Recursively finds base cells (`*.000`) under `root`, sorted by path. Cell
// directories are located by extension, never by the standard
// ENC_ROOT/<producer>/<cell>/ layout: the delivered NOAA sets have their cell
// folders lifted out of ENC_ROOT, and a hand-assembled folder must work too.
Status EnumerateEncCells(const std::string& root, std::vector<std::string>* out);

// Structured cell name: "US5CHSDC" = producer US, usage band 5, region CHS,
// cell id DC. The usage band is S-57's scale band, the role DNC's library plays.
struct S57CellName {
  bool valid = false;
  std::string producer;   // 2 chars
  int usage_band = 0;     // 1 overview … 6 berthing
  std::string region;     // 3 chars
  std::string id;         // remainder
};
S57CellName ParseCellName(const std::string& path_or_filename);

}  // namespace fv
