// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_s57.cpp — see fv_s57.h.

#include "fv_s57.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace fv {
namespace {

// S-57 record names (RCNM).
constexpr int kRcnmDataSetId = 10;
constexpr int kRcnmDataSetParam = 20;
constexpr int kRcnmFeature = 100;
constexpr int kRcnmIsolatedNode = 110;
constexpr int kRcnmConnectedNode = 120;
constexpr int kRcnmEdge = 130;
constexpr int kRcnmFace = 140;

// VRPT.TOPI — which end of an edge a pointer refers to.
constexpr int kTopiBeginNode = 1;
constexpr int kTopiEndNode = 2;

// FSPT.USAG — a ring's role in an area feature.
constexpr int kUsageInterior = 2;

// FSPT/VRPT.ORNT — 2 means the referenced edge runs against the feature.
constexpr int kOrientationReverse = 2;

// A spatial record's key: RCNM in the high byte, RCID below. This is exactly
// what an S-57 NAME subfield (B(40), 5 bytes) encodes.
uint64_t SpatialKey(int rcnm, uint32_t rcid) {
  return (static_cast<uint64_t>(rcnm) << 32) | rcid;
}

uint64_t DecodeName(const iso8211::Value* v) {
  if (v == nullptr || v->kind != iso8211::ValueKind::kBytes || v->bytes.size() < 5)
    return 0;
  const uint32_t rcid = static_cast<uint32_t>(v->bytes[1]) |
                        (static_cast<uint32_t>(v->bytes[2]) << 8) |
                        (static_cast<uint32_t>(v->bytes[3]) << 16) |
                        (static_cast<uint32_t>(v->bytes[4]) << 24);
  return SpatialKey(v->bytes[0], rcid);
}

// LNAM is B(64): AGEN (2) + FIDN (4) + FIDS (2), all LSB first.
S57ObjectId DecodeLnam(const iso8211::Value* v) {
  S57ObjectId id;
  if (v == nullptr || v->kind != iso8211::ValueKind::kBytes || v->bytes.size() < 8)
    return id;
  const uint8_t* b = v->bytes.data();
  id.agency = b[0] | (b[1] << 8);
  id.feature_id = static_cast<uint32_t>(b[2]) | (static_cast<uint32_t>(b[3]) << 8) |
                  (static_cast<uint32_t>(b[4]) << 16) |
                  (static_cast<uint32_t>(b[5]) << 24);
  id.subdivision = b[6] | (b[7] << 8);
  return id;
}

void Grow(GeoRect* r, const S57Vertex& v) {
  r->ll.lat = std::min(r->ll.lat, v.lat);
  r->ll.lon = std::min(r->ll.lon, v.lon);
  r->ur.lat = std::max(r->ur.lat, v.lat);
  r->ur.lon = std::max(r->ur.lon, v.lon);
}

bool SamePoint(const S57Vertex& a, const S57Vertex& b) {
  // S-57 coordinates are integers scaled by COMF, so shared nodes are exactly
  // equal after scaling; an epsilon here would merge distinct vertices.
  return a.lat == b.lat && a.lon == b.lon;
}

// The extension without its dot ("000"). Digits, so no case folding is needed.
std::string FileExtension(const fs::path& p) {
  std::string ext = p.extension().string();
  if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
  return ext;
}

}  // namespace

const S57Attribute* S57Feature::FindAttribute(int code) const {
  for (const S57Attribute& a : attributes)
    if (a.code == code) return &a;
  return nullptr;
}

std::string S57Cell::StalenessWarning() const {
  if (updates_.empty()) return {};
  std::string names;
  for (const std::string& u : updates_) {
    if (!names.empty()) names += ", ";
    names += fs::path(u).filename().string();
  }
  return "S-57 cell " + fs::path(path_).filename().string() +
         " is the BASE EDITION only: " + std::to_string(updates_.size()) +
         " update file(s) on disk were not applied (" + names +
         "). The chart shown is out of date.";
}

Status S57Cell::Open(const std::string& path) {
  is_open_ = false;
  path_ = path;
  info_ = S57DatasetInfo();
  counts_ = S57ParseCounts();
  features_.clear();
  nodes_.clear();
  edges_.clear();
  updates_.clear();
  bounds_ = GeoRect{{90.0, 180.0}, {-90.0, -180.0}};

  Status s = ReadRecords();
  if (!s.ok()) return s;

  // Update files: <stem>.001, .002, … next to the base cell, in sequence. Stop
  // at the first gap — updates after a missing one cannot be applied anyway.
  const fs::path p(path);
  if (FileExtension(p) == "000") {
    for (int n = 1; n <= 999; ++n) {
      char ext[8];
      snprintf(ext, sizeof(ext), ".%03d", n);
      fs::path candidate = p;
      candidate.replace_extension(ext);
      std::error_code ec;
      if (!fs::is_regular_file(candidate, ec)) break;
      updates_.push_back(candidate.string());
    }
  }
  if (!updates_.empty())
    fprintf(stderr, "fv_s57: WARNING: %s\n", StalenessWarning().c_str());

  is_open_ = true;
  return Status::Ok();
}

Status S57Cell::ReadRecords() {
  iso8211::Reader reader;
  Status s = reader.Open(path_);
  if (!s.ok()) return s;

  // Pass 1: dataset records and the vector (spatial) records, which features
  // point at. S-57 writes spatial records before the features that use them,
  // but nothing in the standard guarantees it, so features are collected in
  // this pass and their geometry assembled in pass 2.
  std::vector<iso8211::Record> feature_records;
  iso8211::Record rec;
  while (true) {
    s = reader.Next(&rec);
    if (s.code == kNotFound) break;
    if (!s.ok()) return s;

    if (const iso8211::Field* f = rec.Find("DSID")) {
      info_.name = f->Text(0, "DSNM");
      info_.edition = f->Text(0, "EDTN");
      info_.update_number = f->Text(0, "UPDN");
      info_.update_date = f->Text(0, "UADT");
      info_.issue_date = f->Text(0, "ISDT");
      info_.standard_edition = f->Text(0, "STED");
      info_.product_edition = f->Text(0, "PRED");
      info_.comment = f->Text(0, "COMT");
      info_.producer_agency = static_cast<int>(f->Int(0, "AGEN"));
      info_.intended_usage = static_cast<int>(f->Int(0, "INTU"));
      if (f->Int(0, "RCNM", kRcnmDataSetId) != kRcnmDataSetId)
        return Status::Error(kIoError, "fv_s57: DSID with wrong RCNM in " + path_);
    }
    if (const iso8211::Field* f = rec.Find("DSSI")) {
      info_.data_structure = static_cast<int>(f->Int(0, "DSTR"));
      info_.attribute_lexical_level = static_cast<int>(f->Int(0, "AALL"));
      info_.national_lexical_level = static_cast<int>(f->Int(0, "NALL"));
      info_.declared_meta_records = static_cast<int>(f->Int(0, "NOMR"));
      info_.declared_cartographic_records = static_cast<int>(f->Int(0, "NOCR"));
      info_.declared_geo_records = static_cast<int>(f->Int(0, "NOGR"));
      info_.declared_collection_records = static_cast<int>(f->Int(0, "NOLR"));
      info_.declared_isolated_nodes = static_cast<int>(f->Int(0, "NOIN"));
      info_.declared_connected_nodes = static_cast<int>(f->Int(0, "NOCN"));
      info_.declared_edges = static_cast<int>(f->Int(0, "NOED"));
      info_.declared_faces = static_cast<int>(f->Int(0, "NOFA"));
    }
    if (const iso8211::Field* f = rec.Find("DSPM")) {
      info_.compilation_scale = static_cast<int32_t>(f->Int(0, "CSCL"));
      info_.coordinate_multiplier = f->Real(0, "COMF", 1.0);
      info_.sounding_multiplier = f->Real(0, "SOMF", 1.0);
      info_.horizontal_datum = static_cast<int>(f->Int(0, "HDAT"));
      info_.vertical_datum = static_cast<int>(f->Int(0, "VDAT"));
      info_.sounding_datum = static_cast<int>(f->Int(0, "SDAT"));
      info_.depth_units = static_cast<int>(f->Int(0, "DUNI"));
      info_.height_units = static_cast<int>(f->Int(0, "HUNI"));
      info_.position_units = static_cast<int>(f->Int(0, "PUNI"));
      info_.coordinate_units = static_cast<int>(f->Int(0, "COUN"));
      if (info_.coordinate_multiplier == 0.0 || info_.sounding_multiplier == 0.0)
        return Status::Error(kIoError, "fv_s57: zero COMF/SOMF in " + path_);
      if (f->Int(0, "RCNM", kRcnmDataSetParam) != kRcnmDataSetParam)
        return Status::Error(kIoError, "fv_s57: DSPM with wrong RCNM in " + path_);
    }

    const iso8211::Field* vrid = rec.Find("VRID");
    if (vrid != nullptr) {
      const int rcnm = static_cast<int>(vrid->Int(0, "RCNM"));
      const uint64_t key =
          SpatialKey(rcnm, static_cast<uint32_t>(vrid->Int(0, "RCID")));
      const double comf = info_.coordinate_multiplier;
      const double somf = info_.sounding_multiplier;

      std::vector<S57Vertex> pts;
      for (const iso8211::Field* f : rec.FindAll("SG2D")) {
        for (size_t r = 0; r < f->row_count(); ++r) {
          S57Vertex v;
          v.lat = static_cast<double>(f->Int(r, "YCOO")) / comf;
          v.lon = static_cast<double>(f->Int(r, "XCOO")) / comf;
          pts.push_back(v);
        }
      }
      for (const iso8211::Field* f : rec.FindAll("SG3D")) {
        for (size_t r = 0; r < f->row_count(); ++r) {
          S57Vertex v;
          v.lat = static_cast<double>(f->Int(r, "YCOO")) / comf;
          v.lon = static_cast<double>(f->Int(r, "XCOO")) / comf;
          v.depth = static_cast<double>(f->Int(r, "VE3D")) / somf;
          v.has_depth = true;
          pts.push_back(v);
        }
      }

      if (rcnm == kRcnmIsolatedNode || rcnm == kRcnmConnectedNode) {
        nodes_[key] = std::move(pts);
        if (rcnm == kRcnmIsolatedNode)
          ++counts_.isolated_nodes;
        else
          ++counts_.connected_nodes;
      } else if (rcnm == kRcnmEdge) {
        Edge e;
        e.interior = std::move(pts);
        for (const iso8211::Field* f : rec.FindAll("VRPT")) {
          for (size_t r = 0; r < f->row_count(); ++r) {
            const uint64_t target = DecodeName(f->Get(r, "NAME"));
            const int topi = static_cast<int>(f->Int(r, "TOPI"));
            if (topi == kTopiBeginNode) e.begin = target;
            else if (topi == kTopiEndNode) e.end = target;
          }
        }
        edges_[key] = std::move(e);
        ++counts_.edges;
      } else if (rcnm == kRcnmFace) {
        ++counts_.faces;  // ENC is chain-node (DSTR 2); faces carry no geometry
      }
      continue;
    }

    if (rec.Has("FRID")) feature_records.push_back(std::move(rec));
  }

  // Pass 2: features.
  features_.reserve(feature_records.size());
  for (const iso8211::Record& r : feature_records) {
    const iso8211::Field* frid = r.Find("FRID");
    S57Feature feat;
    feat.record_id = static_cast<int32_t>(frid->Int(0, "RCID"));
    feat.primitive = static_cast<S57Primitive>(frid->Int(0, "PRIM", 255));
    feat.object_class = static_cast<int>(frid->Int(0, "OBJL"));
    feat.group = static_cast<int>(frid->Int(0, "GRUP"));
    feat.version = static_cast<int>(frid->Int(0, "RVER"));
    if (frid->Int(0, "RCNM", kRcnmFeature) != kRcnmFeature)
      return Status::Error(kIoError, "fv_s57: FRID with wrong RCNM in " + path_);

    if (const iso8211::Field* foid = r.Find("FOID")) {
      feat.object_id.agency = static_cast<int>(foid->Int(0, "AGEN"));
      feat.object_id.feature_id = static_cast<uint32_t>(foid->Int(0, "FIDN"));
      feat.object_id.subdivision = static_cast<int>(foid->Int(0, "FIDS"));
    }

    for (const char* tag : {"ATTF", "NATF"}) {
      const bool national = tag[0] == 'N';
      for (const iso8211::Field* f : r.FindAll(tag)) {
        for (size_t row = 0; row < f->row_count(); ++row) {
          S57Attribute a;
          a.code = static_cast<int>(f->Int(row, "ATTL"));
          a.value = f->Text(row, "ATVL");
          a.national = national;
          if (a.code != 0) feat.attributes.push_back(std::move(a));
        }
      }
    }

    for (const iso8211::Field* f : r.FindAll("FFPT")) {
      for (size_t row = 0; row < f->row_count(); ++row) {
        S57FeatureRelation rel;
        rel.target = DecodeLnam(f->Get(row, "LNAM"));
        rel.relation = static_cast<int>(f->Int(row, "RIND"));
        rel.comment = f->Text(row, "COMT");
        feat.relations.push_back(std::move(rel));
      }
    }

    AssembleGeometry(r, &feat);

    ++counts_.feature_records;
    if (feat.primitive == S57Primitive::kNone) ++counts_.collection_records;
    for (const std::vector<S57Vertex>& part : feat.geometry.parts)
      for (const S57Vertex& v : part) Grow(&bounds_, v);
    features_.push_back(std::move(feat));
  }

  return Status::Ok();
}

bool S57Cell::EdgePoints(uint64_t key, bool reverse,
                         std::vector<S57Vertex>* out, bool* complete) const {
  auto it = edges_.find(key);
  if (it == edges_.end()) return false;
  const Edge& e = it->second;
  out->clear();
  auto push_node = [this, out, complete](uint64_t node_key) {
    auto nit = nodes_.find(node_key);
    if (nit != nodes_.end() && !nit->second.empty())
      out->push_back(nit->second.front());
    else
      *complete = false;  // an end node we cannot resolve loses a vertex
  };
  push_node(e.begin);
  out->insert(out->end(), e.interior.begin(), e.interior.end());
  push_node(e.end);
  if (reverse) std::reverse(out->begin(), out->end());
  return !out->empty();
}

void S57Cell::AssembleGeometry(const iso8211::Record& record,
                               S57Feature* feature) const {
  // Every spatial pointer of the feature, flattened across FSPT occurrences.
  struct Pointer {
    uint64_t key;
    bool reverse;
    int usage;
  };
  std::vector<Pointer> pointers;
  for (const iso8211::Field* f : record.FindAll("FSPT")) {
    for (size_t r = 0; r < f->row_count(); ++r) {
      Pointer p;
      p.key = DecodeName(f->Get(r, "NAME"));
      p.reverse = f->Int(r, "ORNT") == kOrientationReverse;
      p.usage = static_cast<int>(f->Int(r, "USAG", 255));
      // MASK (1 = masked) is deliberately ignored: masking says which parts of
      // a boundary S-52 draws, not which parts exist. A renderer that wants it
      // must read FSPT itself.
      pointers.push_back(p);
    }
  }

  S57Geometry& geom = feature->geometry;
  switch (feature->primitive) {
    case S57Primitive::kPoint: {
      for (const Pointer& p : pointers) {
        auto it = nodes_.find(p.key);
        if (it == nodes_.end() || it->second.empty()) {
          feature->geometry_complete = false;
          continue;
        }
        // One part per node; a sounding node carries the whole array.
        geom.parts.push_back(it->second);
        geom.part_is_hole.push_back(false);
      }
      break;
    }
    case S57Primitive::kLine: {
      std::vector<S57Vertex> run;
      for (const Pointer& p : pointers) {
        std::vector<S57Vertex> pts;
        if (!EdgePoints(p.key, p.reverse, &pts, &feature->geometry_complete)) {
          feature->geometry_complete = false;
          continue;
        }
        if (!run.empty() && SamePoint(run.back(), pts.front())) {
          run.insert(run.end(), pts.begin() + 1, pts.end());
        } else {
          if (!run.empty()) {
            geom.parts.push_back(run);
            geom.part_is_hole.push_back(false);
          }
          run = std::move(pts);
        }
      }
      if (!run.empty()) {
        geom.parts.push_back(std::move(run));
        geom.part_is_hole.push_back(false);
      }
      break;
    }
    case S57Primitive::kArea: {
      // Chain-node topology: FSPT lists the boundary edges in ring order, and a
      // ring ends where it returns to its own first vertex. USAG says which
      // rings are holes (1 exterior, 2 interior, 3 exterior truncated by the
      // data limit — 3 is still an exterior boundary).
      std::vector<S57Vertex> ring;
      bool ring_is_hole = false;
      for (const Pointer& p : pointers) {
        std::vector<S57Vertex> pts;
        if (!EdgePoints(p.key, p.reverse, &pts, &feature->geometry_complete)) {
          feature->geometry_complete = false;
          continue;
        }
        if (ring.empty()) {
          ring = std::move(pts);
          ring_is_hole = p.usage == kUsageInterior;
        } else if (SamePoint(ring.back(), pts.front())) {
          ring.insert(ring.end(), pts.begin() + 1, pts.end());
        } else {
          // A gap inside a ring: keep the vertices (the boundary is still the
          // producer's) and note the feature as incomplete.
          ring.insert(ring.end(), pts.begin(), pts.end());
          feature->geometry_complete = false;
        }
        if (ring.size() > 2 && SamePoint(ring.front(), ring.back())) {
          geom.parts.push_back(std::move(ring));
          geom.part_is_hole.push_back(ring_is_hole);
          ring.clear();
        }
      }
      if (!ring.empty()) {  // never closed
        geom.parts.push_back(std::move(ring));
        geom.part_is_hole.push_back(ring_is_hole);
        feature->geometry_complete = false;
      }
      break;
    }
    case S57Primitive::kNone:
      // Meta features (M_COVR, C_AGGR …) may still carry an area boundary; the
      // ones that do are rare and phase E3 decides what to do with them.
      break;
  }

  GeoRect b{{90.0, 180.0}, {-90.0, -180.0}};
  bool any = false;
  for (const std::vector<S57Vertex>& part : geom.parts)
    for (const S57Vertex& v : part) {
      Grow(&b, v);
      any = true;
    }
  if (any) feature->bounds = b;
}

// ---------------------------------------------------------------------------

Status ReadS57Catalog(const std::string& path, std::vector<S57CatalogEntry>* out) {
  out->clear();
  iso8211::Reader reader;
  Status s = reader.Open(path);
  if (!s.ok()) return s;
  if (reader.Definition("CATD") == nullptr)
    return Status::Error(kIoError, "fv_s57: no CATD field in " + path);

  iso8211::Record rec;
  while (true) {
    s = reader.Next(&rec);
    if (s.code == kNotFound) break;
    if (!s.ok()) return s;
    const iso8211::Field* f = rec.Find("CATD");
    if (f == nullptr) continue;
    for (size_t r = 0; r < f->row_count(); ++r) {
      S57CatalogEntry e;
      e.record_id = static_cast<int32_t>(f->Int(r, "RCID"));
      e.file = f->Text(r, "FILE");
      e.long_file_name = f->Text(r, "LFIL");
      e.volume = f->Text(r, "VOLM");
      e.implementation = f->Text(r, "IMPL");
      e.crc = f->Text(r, "CRCS");
      e.comment = f->Text(r, "COMT");
      const iso8211::Value* slat = f->Get(r, "SLAT");
      const iso8211::Value* wlon = f->Get(r, "WLON");
      const iso8211::Value* nlat = f->Get(r, "NLAT");
      const iso8211::Value* elon = f->Get(r, "ELON");
      if (slat != nullptr && wlon != nullptr && nlat != nullptr &&
          elon != nullptr && !slat->null && !wlon->null && !nlat->null &&
          !elon->null) {
        e.bounds.ll.lat = slat->AsDouble();
        e.bounds.ll.lon = wlon->AsDouble();
        e.bounds.ur.lat = nlat->AsDouble();
        e.bounds.ur.lon = elon->AsDouble();
        e.has_bounds = true;
      }
      out->push_back(std::move(e));
    }
  }
  return Status::Ok();
}

Status EnumerateEncCells(const std::string& root, std::vector<std::string>* out) {
  out->clear();
  std::error_code ec;
  if (!fs::is_directory(root, ec))
    return Status::Error(kNotFound, "fv_s57: not a directory: " + root);
  fs::recursive_directory_iterator it(root, ec), end;
  if (ec) return Status::Error(kIoError, "fv_s57: cannot scan " + root);
  for (; it != end; it.increment(ec)) {
    if (ec) return Status::Error(kIoError, "fv_s57: scan failed under " + root);
    if (!it->is_regular_file(ec)) continue;
    if (FileExtension(it->path()) == "000") out->push_back(it->path().string());
  }
  std::sort(out->begin(), out->end());
  return Status::Ok();
}

S57CellName ParseCellName(const std::string& path_or_filename) {
  S57CellName n;
  std::string stem = fs::path(path_or_filename).stem().string();
  if (stem.size() < 6) return n;
  for (char& c : stem) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  if (stem[2] < '1' || stem[2] > '9') return n;
  n.producer = stem.substr(0, 2);
  n.usage_band = stem[2] - '0';
  n.region = stem.substr(3, 3);
  n.id = stem.substr(6);
  n.valid = true;
  return n;
}

}  // namespace fv
