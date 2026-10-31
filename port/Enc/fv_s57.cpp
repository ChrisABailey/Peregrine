// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_s57.cpp — see fv_s57.h.

#include "fv_s57.h"

#include "fv_s57_records.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "fvkit/log.h"
#include "fvkit/formats/scan_interrupt.h"

namespace fs = std::filesystem;

namespace fv {
namespace {

constexpr int kRcnmDataSetId = 10;
constexpr int kRcnmDataSetParam = 20;
constexpr int kRcnmIsolatedNode = kS57RcnmIsolatedNode;
constexpr int kRcnmConnectedNode = kS57RcnmConnectedNode;
constexpr int kRcnmEdge = kS57RcnmEdge;
constexpr int kRcnmFace = kS57RcnmFace;

// VRPT.TOPI — which end of an edge a pointer refers to.
constexpr int kTopiBeginNode = 1;
constexpr int kTopiEndNode = 2;

// FSPT.USAG — a ring's role in an area feature.
constexpr int kUsageInterior = 2;

// FSPT/VRPT.ORNT — 2 means the referenced edge runs against the feature.
constexpr int kOrientationReverse = 2;

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

// Copies the DSID, DSSI and DSPM fields of `rec`, when present, into `info`.
Status ReadDatasetFields(const iso8211::Record& rec, const std::string& path,
                         S57DatasetInfo* info) {
  if (const iso8211::Field* f = rec.Find("DSID")) {
    info->name = f->Text(0, "DSNM");
    info->edition = f->Text(0, "EDTN");
    info->update_number = f->Text(0, "UPDN");
    info->update_date = f->Text(0, "UADT");
    info->issue_date = f->Text(0, "ISDT");
    info->standard_edition = f->Text(0, "STED");
    info->product_edition = f->Text(0, "PRED");
    info->comment = f->Text(0, "COMT");
    info->producer_agency = static_cast<int>(f->Int(0, "AGEN"));
    info->intended_usage = static_cast<int>(f->Int(0, "INTU"));
    if (f->Int(0, "RCNM", kRcnmDataSetId) != kRcnmDataSetId)
      return Status::Error(kIoError, "fv_s57: DSID with wrong RCNM in " + path);
  }
  if (const iso8211::Field* f = rec.Find("DSSI")) {
    info->data_structure = static_cast<int>(f->Int(0, "DSTR"));
    info->attribute_lexical_level = static_cast<int>(f->Int(0, "AALL"));
    info->national_lexical_level = static_cast<int>(f->Int(0, "NALL"));
    info->declared_meta_records = static_cast<int>(f->Int(0, "NOMR"));
    info->declared_cartographic_records = static_cast<int>(f->Int(0, "NOCR"));
    info->declared_geo_records = static_cast<int>(f->Int(0, "NOGR"));
    info->declared_collection_records = static_cast<int>(f->Int(0, "NOLR"));
    info->declared_isolated_nodes = static_cast<int>(f->Int(0, "NOIN"));
    info->declared_connected_nodes = static_cast<int>(f->Int(0, "NOCN"));
    info->declared_edges = static_cast<int>(f->Int(0, "NOED"));
    info->declared_faces = static_cast<int>(f->Int(0, "NOFA"));
  }
  if (const iso8211::Field* f = rec.Find("DSPM")) {
    info->compilation_scale = static_cast<int32_t>(f->Int(0, "CSCL"));
    info->coordinate_multiplier = f->Real(0, "COMF", 1.0);
    info->sounding_multiplier = f->Real(0, "SOMF", 1.0);
    info->horizontal_datum = static_cast<int>(f->Int(0, "HDAT"));
    info->vertical_datum = static_cast<int>(f->Int(0, "VDAT"));
    info->sounding_datum = static_cast<int>(f->Int(0, "SDAT"));
    info->depth_units = static_cast<int>(f->Int(0, "DUNI"));
    info->height_units = static_cast<int>(f->Int(0, "HUNI"));
    info->position_units = static_cast<int>(f->Int(0, "PUNI"));
    info->coordinate_units = static_cast<int>(f->Int(0, "COUN"));
    if (info->coordinate_multiplier == 0.0 || info->sounding_multiplier == 0.0)
      return Status::Error(kIoError, "fv_s57: zero COMF/SOMF in " + path);
    if (f->Int(0, "RCNM", kRcnmDataSetParam) != kRcnmDataSetParam)
      return Status::Error(kIoError, "fv_s57: DSPM with wrong RCNM in " + path);
  }
  return Status::Ok();
}

// Reads every record of the cell at `path`: dataset fields into `info`,
// feature and spatial records into `records`.
Status ReadBaseRecords(const std::string& path, S57DatasetInfo* info,
                       S57RecordSet* records) {
  iso8211::Reader reader;
  Status s = reader.Open(path);
  if (!s.ok()) return s;

  // All records are collected before any geometry is built: S-57 writes
  // spatial records before the features that use them, but nothing in the
  // standard guarantees it, and updates edit both kinds.
  iso8211::Record rec;
  while (true) {
    s = reader.Next(&rec);
    if (s.code == kNotFound) break;
    if (!s.ok()) return s;
    s = ReadDatasetFields(rec, path, info);
    if (!s.ok()) return s;
    s = records->AddBaseRecord(rec, path);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

// Applies `files` in order until one fails or the cell is cancelled. Returns
// how many applied; `*error` gets the failing file's reason.
size_t ApplyUpdateFiles(const std::vector<std::string>& files, S57DatasetInfo* info,
                        S57RecordSet* records, std::string* error) {
  size_t applied = 0;
  for (; applied < files.size() && !info->cancelled; ++applied) {
    const Status s = ApplyS57Update(files[applied], info, records);
    if (!s.ok()) {
      *error = s.message;
      break;
    }
  }
  return applied;
}

}  // namespace

const S57Attribute* S57Feature::FindAttribute(int code) const {
  for (const S57Attribute& a : attributes)
    if (a.code == code) return &a;
  return nullptr;
}

std::string S57Cell::StalenessWarning() const {
  const std::string cell = fs::path(path_).filename().string();
  if (info_.cancelled)
    return "S-57 cell " + cell + " was cancelled by update " + info_.update_number +
           " and is not drawn.";
  if (updates_.empty()) return {};
  std::string names;
  for (const std::string& u : updates_) {
    if (!names.empty()) names += ", ";
    names += fs::path(u).filename().string();
  }
  std::string w = "S-57 cell " + cell + " is at update " + info_.update_number + ": " +
                  std::to_string(updates_.size()) + " update file(s) were not applied (" +
                  names + ")";
  if (!update_error_.empty()) w += "; " + update_error_;
  return w + ". The chart shown is out of date.";
}

Status S57Cell::Open(const std::string& path, S57Updates updates) {
  is_open_ = false;
  path_ = path;
  info_ = S57DatasetInfo();
  counts_ = S57ParseCounts();
  features_.clear();
  nodes_.clear();
  edges_.clear();
  updates_.clear();
  update_error_.clear();
  bounds_ = GeoRect{{90.0, 180.0}, {-90.0, -180.0}};

  S57RecordSet records;
  Status s = ReadRecords(&records);
  if (!s.ok()) return s;

  updates_ = FindS57UpdateFiles(path);
  if (updates == S57Updates::kApply) {
    const size_t applied = ApplyUpdateFiles(updates_, &info_, &records, &update_error_);
    updates_.erase(updates_.begin(), updates_.begin() + applied);
  }
  if (!info_.cancelled) BuildFeatures(records);
  // Spatial records are only read through by geometry assembly; a whole
  // exchange set held open would otherwise keep every edge twice.
  decltype(nodes_)().swap(nodes_);
  decltype(edges_)().swap(edges_);

  if (!updates_.empty() || info_.cancelled)
    FV_LOG_WARNING("S-57: " << StalenessWarning());

  is_open_ = true;
  return Status::Ok();
}

Status S57Cell::ReadRecords(S57RecordSet* records) {
  return ReadBaseRecords(path_, &info_, records);
}

void S57Cell::BuildFeatures(const S57RecordSet& records) {
  const double comf = info_.coordinate_multiplier;
  const double somf = info_.sounding_multiplier;
  for (const auto& [key, v] : records.vectors) {
    std::vector<S57Vertex> pts;
    pts.reserve(v.coordinates.size());
    for (const S57RawCoordinate& c : v.coordinates) {
      S57Vertex vx;
      vx.lat = static_cast<double>(c.y) / comf;
      vx.lon = static_cast<double>(c.x) / comf;
      if (c.has_z) {
        vx.depth = static_cast<double>(c.z) / somf;
        vx.has_depth = true;
      }
      pts.push_back(vx);
    }
    if (v.rcnm == kRcnmIsolatedNode || v.rcnm == kRcnmConnectedNode) {
      nodes_[key] = std::move(pts);
      if (v.rcnm == kRcnmIsolatedNode)
        ++counts_.isolated_nodes;
      else
        ++counts_.connected_nodes;
    } else if (v.rcnm == kRcnmEdge) {
      Edge e;
      e.interior = std::move(pts);
      for (const S57SpatialPointer& p : v.pointers) {
        if (p.topology == kTopiBeginNode) e.begin = p.name;
        else if (p.topology == kTopiEndNode) e.end = p.name;
      }
      edges_[key] = std::move(e);
      ++counts_.edges;
    } else if (v.rcnm == kRcnmFace) {
      ++counts_.faces;  // ENC is chain-node (DSTR 2); faces carry no geometry
    }
  }

  features_.reserve(records.features.size());
  for (const S57FeatureRecord& r : records.features) {
    if (r.deleted) continue;
    S57Feature feat;
    feat.record_id = static_cast<int32_t>(r.rcid);
    feat.primitive = static_cast<S57Primitive>(r.primitive);
    feat.object_class = r.object_class;
    feat.group = r.group;
    feat.version = r.version;
    feat.object_id = r.object_id;
    feat.attributes = r.attributes;
    feat.relations = r.relations;
    AssembleGeometry(r, &feat);

    ++counts_.feature_records;
    if (feat.primitive == S57Primitive::kNone) ++counts_.collection_records;
    for (const std::vector<S57Vertex>& part : feat.geometry.parts)
      for (const S57Vertex& v : part) Grow(&bounds_, v);
    features_.push_back(std::move(feat));
  }
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

void S57Cell::AssembleGeometry(const S57FeatureRecord& record,
                               S57Feature* feature) const {
  struct Pointer {
    uint64_t key;
    bool reverse;
    int usage;
  };
  std::vector<Pointer> pointers;
  pointers.reserve(record.pointers.size());
  // MASK (1 = masked) is ignored: masking says which parts of a boundary
  // S-52 draws, not which parts exist.
  for (const S57SpatialPointer& sp : record.pointers)
    pointers.push_back({sp.name, sp.orientation == kOrientationReverse, sp.usage});

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

std::vector<std::string> FindS57UpdateFiles(const std::string& base_cell) {
  // <stem>.001, .002, … in sequence; stop at the first gap, since updates
  // after a missing one cannot be applied anyway.
  std::vector<std::string> out;
  const fs::path p(base_cell);
  if (FileExtension(p) != "000") return out;
  for (int n = 1; n <= 999; ++n) {
    char ext[8];
    snprintf(ext, sizeof(ext), ".%03d", n);
    fs::path candidate = p;
    candidate.replace_extension(ext);
    std::error_code ec;
    if (!fs::is_regular_file(candidate, ec)) break;
    out.push_back(candidate.string());
  }
  return out;
}

Status ReadS57DatasetInfo(const std::string& path, S57DatasetInfo* out) {
  *out = S57DatasetInfo();
  iso8211::Reader reader;
  Status s = reader.Open(path);
  if (!s.ok()) return s;
  iso8211::Record rec;
  bool have_dsid = false;
  while (true) {
    s = reader.Next(&rec);
    if (s.code == kNotFound) break;
    if (!s.ok()) return s;
    have_dsid = have_dsid || rec.Has("DSID");
    s = ReadDatasetFields(rec, path, out);
    if (!s.ok()) return s;
    // DSPM follows DSID; anything after it is spatial or feature data.
    if (rec.Has("DSPM")) break;
  }
  if (!have_dsid) return Status::Error(kIoError, "fv_s57: no DSID record in " + path);
  return Status::Ok();
}

Status ReadS57UpdateState(const std::string& base_cell, S57UpdateState* out) {
  *out = S57UpdateState();
  S57RecordSet records;
  Status s = ReadBaseRecords(base_cell, &out->info, &records);
  if (!s.ok()) return s;
  const std::vector<std::string> files = FindS57UpdateFiles(base_cell);
  out->update_files = static_cast<int>(files.size());
  out->applied =
      static_cast<int>(ApplyUpdateFiles(files, &out->info, &records, &out->error));
  return Status::Ok();
}

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
    if (ScanInterrupted()) return Status::Error(kInterrupted, "scan cancelled");
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
