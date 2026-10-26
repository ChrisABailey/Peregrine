// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_s57_records.cpp — see fv_s57_records.h.

#include "fv_s57_records.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace fv {
namespace {

// Record update instructions (RUIN) and field update instructions (*UI).
constexpr int kInsert = 1;
constexpr int kDelete = 2;
constexpr int kModify = 3;

uint64_t DecodeName(const iso8211::Value* v) {
  if (v == nullptr || v->kind != iso8211::ValueKind::kBytes || v->bytes.size() < 5)
    return 0;
  const uint32_t rcid = static_cast<uint32_t>(v->bytes[1]) |
                        (static_cast<uint32_t>(v->bytes[2]) << 8) |
                        (static_cast<uint32_t>(v->bytes[3]) << 16) |
                        (static_cast<uint32_t>(v->bytes[4]) << 24);
  return S57SpatialKey(v->bytes[0], rcid);
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

std::vector<S57SpatialPointer> ReadPointers(const iso8211::Field& f) {
  std::vector<S57SpatialPointer> out;
  out.reserve(f.row_count());
  for (size_t r = 0; r < f.row_count(); ++r) {
    S57SpatialPointer p;
    p.name = DecodeName(f.Get(r, "NAME"));
    p.orientation = static_cast<int>(f.Int(r, "ORNT", 255));
    p.usage = static_cast<int>(f.Int(r, "USAG", 255));
    p.topology = static_cast<int>(f.Int(r, "TOPI", 255));
    p.mask = static_cast<int>(f.Int(r, "MASK", 255));
    out.push_back(p);
  }
  return out;
}

std::vector<S57RawCoordinate> ReadCoordinates(const iso8211::Field& f) {
  const bool three_d = f.tag() == "SG3D";
  std::vector<S57RawCoordinate> out;
  out.reserve(f.row_count());
  for (size_t r = 0; r < f.row_count(); ++r) {
    S57RawCoordinate c;
    c.y = f.Int(r, "YCOO");
    c.x = f.Int(r, "XCOO");
    if (three_d) {
      c.z = f.Int(r, "VE3D");
      c.has_z = true;
    }
    out.push_back(c);
  }
  return out;
}

std::vector<S57FeatureRelation> ReadRelations(const iso8211::Field& f) {
  std::vector<S57FeatureRelation> out;
  for (size_t r = 0; r < f.row_count(); ++r) {
    S57FeatureRelation rel;
    rel.target = DecodeLnam(f.Get(r, "LNAM"));
    rel.relation = static_cast<int>(f.Int(r, "RIND"));
    rel.comment = f.Text(r, "COMT");
    out.push_back(std::move(rel));
  }
  return out;
}

std::vector<S57Attribute> ReadAttributes(const iso8211::Field& f) {
  const bool national = f.tag() == "NATF";
  std::vector<S57Attribute> out;
  for (size_t r = 0; r < f.row_count(); ++r) {
    S57Attribute a;
    a.code = static_cast<int>(f.Int(r, "ATTL"));
    a.value = f.Text(r, "ATVL");
    a.national = national;
    if (a.code != 0) out.push_back(std::move(a));
  }
  return out;
}

bool IsAttributeTag(const std::string& tag) {
  return tag == "ATTF" || tag == "NATF" || tag == "ATTV";
}

S57VectorRecord ReadVector(const iso8211::Record& rec, const iso8211::Field& vrid) {
  S57VectorRecord v;
  v.rcnm = static_cast<int>(vrid.Int(0, "RCNM"));
  v.rcid = static_cast<uint32_t>(vrid.Int(0, "RCID"));
  v.version = static_cast<int>(vrid.Int(0, "RVER"));
  for (const iso8211::Field& f : rec.fields()) {
    if (f.tag() == "ATTV") {
      for (S57Attribute& a : ReadAttributes(f)) v.attributes.push_back(std::move(a));
    } else if (f.tag() == "VRPT") {
      for (const S57SpatialPointer& p : ReadPointers(f)) v.pointers.push_back(p);
    } else if (f.tag() == "SG2D" || f.tag() == "SG3D") {
      for (const S57RawCoordinate& c : ReadCoordinates(f)) v.coordinates.push_back(c);
    }
  }
  return v;
}

S57FeatureRecord ReadFeature(const iso8211::Record& rec, const iso8211::Field& frid) {
  S57FeatureRecord feat;
  feat.rcid = static_cast<uint32_t>(frid.Int(0, "RCID"));
  feat.primitive = static_cast<int>(frid.Int(0, "PRIM", 255));
  feat.group = static_cast<int>(frid.Int(0, "GRUP"));
  feat.object_class = static_cast<int>(frid.Int(0, "OBJL"));
  feat.version = static_cast<int>(frid.Int(0, "RVER"));
  if (const iso8211::Field* foid = rec.Find("FOID")) {
    feat.object_id.agency = static_cast<int>(foid->Int(0, "AGEN"));
    feat.object_id.feature_id = static_cast<uint32_t>(foid->Int(0, "FIDN"));
    feat.object_id.subdivision = static_cast<int>(foid->Int(0, "FIDS"));
  }
  // ATTF before NATF regardless of field order, as the base reader always had it.
  for (const char* tag : {"ATTF", "NATF"})
    for (const iso8211::Field* f : rec.FindAll(tag))
      for (S57Attribute& a : ReadAttributes(*f)) feat.attributes.push_back(std::move(a));
  for (const iso8211::Field& f : rec.fields()) {
    if (f.tag() == "FFPT") {
      for (S57FeatureRelation& r : ReadRelations(f)) feat.relations.push_back(std::move(r));
    } else if (f.tag() == "FSPT") {
      for (const S57SpatialPointer& p : ReadPointers(f)) feat.pointers.push_back(p);
    }
  }
  return feat;
}


// A value of only the delete character (0x7F; UCS-2 adds 0x00 bytes) removes
// the attribute; any other value, empty included, sets it.
bool IsDeleteValue(const std::string& v) {
  bool any = false;
  for (char c : v) {
    if (c == '\x7f') any = true;
    else if (c != '\0') return false;
  }
  return any;
}

int ParseNumber(const std::string& s, int missing) {
  if (s.empty()) return missing;
  try {
    return std::stoi(s);
  } catch (...) {
    return missing;
  }
}

/** Applies one field-level instruction to a run of rows. `index` is 1-based. */
template <typename T>
Status EditRows(std::vector<T>* rows, int instruction, int64_t index, int64_t count,
                const std::vector<T>& replacement, const std::string& what) {
  const int64_t size = static_cast<int64_t>(rows->size());
  const bool in_range = index >= 1 && count >= 0 && index - 1 + count <= size;
  switch (instruction) {
    case kInsert:
      if (index < 1 || index > size + 1 ||
          static_cast<int64_t>(replacement.size()) != count)
        return Status::Error(kIoError, "fv_s57: bad insert into " + what);
      rows->insert(rows->begin() + (index - 1), replacement.begin(), replacement.end());
      return Status::Ok();
    case kDelete:
      if (!in_range) return Status::Error(kIoError, "fv_s57: bad delete from " + what);
      rows->erase(rows->begin() + (index - 1), rows->begin() + (index - 1 + count));
      return Status::Ok();
    case kModify:
      if (!in_range || static_cast<int64_t>(replacement.size()) != count)
        return Status::Error(kIoError, "fv_s57: bad modify of " + what);
      std::copy(replacement.begin(), replacement.end(), rows->begin() + (index - 1));
      return Status::Ok();
    default:
      return Status::Error(kIoError, "fv_s57: unknown update instruction in " + what);
  }
}

/** Sets, replaces or deletes attributes by (ATTL, national). */
Status EditAttributes(std::vector<S57Attribute>* attrs,
                      const std::vector<S57Attribute>& changes, const std::string& what) {
  for (const S57Attribute& c : changes) {
    auto it = std::find_if(attrs->begin(), attrs->end(), [&c](const S57Attribute& a) {
      return a.code == c.code && a.national == c.national;
    });
    if (IsDeleteValue(c.value)) {
      if (it == attrs->end())
        return Status::Error(kIoError, "fv_s57: deletes an absent attribute in " + what);
      attrs->erase(it);
    } else if (it != attrs->end()) {
      it->value = c.value;
    } else {
      attrs->push_back(c);
    }
  }
  return Status::Ok();
}

/** Prior state of everything one update touched, for rollback. */
struct UndoLog {
  std::unordered_map<uint64_t, std::optional<S57VectorRecord>> vectors;
  std::unordered_map<size_t, S57FeatureRecord> features;  // by features[] index
  std::unordered_map<uint32_t, std::optional<size_t>> feature_index;
  size_t feature_count = 0;

  void SaveVector(const S57RecordSet& set, uint64_t key) {
    if (vectors.count(key)) return;
    auto it = set.vectors.find(key);
    vectors[key] = it == set.vectors.end() ? std::nullopt
                                           : std::optional<S57VectorRecord>(it->second);
  }
  void SaveFeature(const S57RecordSet& set, size_t i) {
    if (i < feature_count && !features.count(i)) features[i] = set.features[i];
  }
  void SaveIndex(const S57RecordSet& set, uint32_t rcid) {
    if (feature_index.count(rcid)) return;
    auto it = set.feature_index.find(rcid);
    feature_index[rcid] = it == set.feature_index.end()
                              ? std::nullopt
                              : std::optional<size_t>(it->second);
  }
  void Restore(S57RecordSet* set) const {
    for (const auto& [key, v] : vectors) {
      if (v) set->vectors[key] = *v;
      else set->vectors.erase(key);
    }
    set->features.resize(feature_count);
    for (const auto& [i, f] : features) set->features[i] = f;
    for (const auto& [rcid, i] : feature_index) {
      if (i) set->feature_index[rcid] = *i;
      else set->feature_index.erase(rcid);
    }
  }
};

/**
 * Walks the control/data field pairs of a modify record. `on_attributes`
 * handles ATTF/NATF/ATTV; `on_control` handles a control field (FSPC, FFPC,
 * VRPC, SGCC) and the data field that follows it, or nullptr for a delete.
 */
template <typename AttrFn, typename ControlFn>
Status WalkModifyFields(const iso8211::Record& rec, const std::string& what,
                        AttrFn on_attributes, ControlFn on_control) {
  const std::vector<iso8211::Field>& fields = rec.fields();
  for (size_t i = 0; i < fields.size(); ++i) {
    const iso8211::Field& f = fields[i];
    const std::string& tag = f.tag();
    if (IsAttributeTag(tag)) {
      Status s = on_attributes(f);
      if (!s.ok()) return s;
    } else if (tag == "FSPC" || tag == "FFPC" || tag == "VRPC" || tag == "SGCC") {
      // Subfields are named per field (FSUI/FSIX/NSPT, …) but always in this order.
      const std::vector<iso8211::Value>& row = f.row(0);
      if (row.size() < 3) return Status::Error(kIoError, "fv_s57: short " + tag + " in " + what);
      const int ui = static_cast<int>(row[0].AsInt());
      const int64_t ix = row[1].AsInt();
      const int64_t n = row[2].AsInt();
      const iso8211::Field* data = nullptr;
      if (ui != kDelete) {
        if (i + 1 >= fields.size())
          return Status::Error(kIoError, "fv_s57: " + tag + " without data in " + what);
        data = &fields[++i];
      }
      Status s = on_control(tag, ui, ix, n, data);
      if (!s.ok()) return s;
    } else if (tag == "FSPT" || tag == "FFPT" || tag == "VRPT" || tag == "SG2D" ||
               tag == "SG3D") {
      return Status::Error(kIoError, "fv_s57: " + tag + " without a control field in " + what);
    }
  }
  return Status::Ok();
}

Status ApplyVectorRecord(const iso8211::Record& rec, const iso8211::Field& vrid,
                         const std::string& what, S57RecordSet* set, UndoLog* undo) {
  const int rcnm = static_cast<int>(vrid.Int(0, "RCNM"));
  const uint32_t rcid = static_cast<uint32_t>(vrid.Int(0, "RCID"));
  const int rver = static_cast<int>(vrid.Int(0, "RVER"));
  const int ruin = static_cast<int>(vrid.Int(0, "RUIN"));
  const uint64_t key = S57SpatialKey(rcnm, rcid);
  const std::string id = what + " vector " + std::to_string(rcnm) + "/" + std::to_string(rcid);
  auto it = set->vectors.find(key);

  undo->SaveVector(*set, key);
  if (ruin == kInsert) {
    if (it != set->vectors.end()) return Status::Error(kIoError, "fv_s57: inserts existing " + id);
    set->vectors[key] = ReadVector(rec, vrid);
    return Status::Ok();
  }
  if (it == set->vectors.end()) return Status::Error(kIoError, "fv_s57: no such " + id);
  if (rver != it->second.version + 1)
    return Status::Error(kIoError, "fv_s57: version mismatch on " + id);
  if (ruin == kDelete) {
    set->vectors.erase(it);
    return Status::Ok();
  }
  if (ruin != kModify) return Status::Error(kIoError, "fv_s57: bad RUIN on " + id);

  S57VectorRecord& v = it->second;
  v.version = rver;
  return WalkModifyFields(
      rec, id,
      [&](const iso8211::Field& f) { return EditAttributes(&v.attributes, ReadAttributes(f), id); },
      [&](const std::string& tag, int ui, int64_t ix, int64_t n, const iso8211::Field* data) {
        if (tag == "VRPC") {
          if (data != nullptr && data->tag() != "VRPT")
            return Status::Error(kIoError, "fv_s57: VRPC not followed by VRPT in " + id);
          return EditRows(&v.pointers, ui, ix, n,
                          data ? ReadPointers(*data) : std::vector<S57SpatialPointer>(), id);
        }
        if (tag == "SGCC") {
          if (data != nullptr && data->tag() != "SG2D" && data->tag() != "SG3D")
            return Status::Error(kIoError, "fv_s57: SGCC not followed by coordinates in " + id);
          return EditRows(&v.coordinates, ui, ix, n,
                          data ? ReadCoordinates(*data) : std::vector<S57RawCoordinate>(), id);
        }
        return Status::Error(kIoError, "fv_s57: " + tag + " in a vector record, " + id);
      });
}

Status ApplyFeatureRecord(const iso8211::Record& rec, const iso8211::Field& frid,
                          const std::string& what, S57RecordSet* set, UndoLog* undo) {
  const uint32_t rcid = static_cast<uint32_t>(frid.Int(0, "RCID"));
  const int rver = static_cast<int>(frid.Int(0, "RVER"));
  const int ruin = static_cast<int>(frid.Int(0, "RUIN"));
  const std::string id = what + " feature " + std::to_string(rcid);
  if (frid.Int(0, "RCNM", kS57RcnmFeature) != kS57RcnmFeature)
    return Status::Error(kIoError, "fv_s57: FRID with wrong RCNM in " + what);
  S57FeatureRecord* existing = set->FindFeature(rcid);

  if (ruin == kInsert) {
    if (existing != nullptr) return Status::Error(kIoError, "fv_s57: inserts existing " + id);
    undo->SaveIndex(*set, rcid);
    set->feature_index[rcid] = set->features.size();
    set->features.push_back(ReadFeature(rec, frid));
    return Status::Ok();
  }
  if (existing == nullptr) return Status::Error(kIoError, "fv_s57: no such " + id);
  if (rver != existing->version + 1)
    return Status::Error(kIoError, "fv_s57: version mismatch on " + id);
  undo->SaveFeature(*set, set->feature_index[rcid]);
  if (ruin == kDelete) {
    existing->deleted = true;
    return Status::Ok();
  }
  if (ruin != kModify) return Status::Error(kIoError, "fv_s57: bad RUIN on " + id);

  // OBJL, PRIM and FOID identify the object; an update may not change them.
  if (frid.Int(0, "OBJL") != existing->object_class ||
      frid.Int(0, "PRIM", 255) != existing->primitive)
    return Status::Error(kIoError, "fv_s57: changes the class or primitive of " + id);
  if (const iso8211::Field* foid = rec.Find("FOID")) {
    if (foid->Int(0, "AGEN") != existing->object_id.agency ||
        static_cast<uint32_t>(foid->Int(0, "FIDN")) != existing->object_id.feature_id ||
        foid->Int(0, "FIDS") != existing->object_id.subdivision)
      return Status::Error(kIoError, "fv_s57: changes the object id of " + id);
  }

  S57FeatureRecord& feat = *existing;
  feat.version = rver;
  return WalkModifyFields(
      rec, id,
      [&](const iso8211::Field& f) {
        return EditAttributes(&feat.attributes, ReadAttributes(f), id);
      },
      [&](const std::string& tag, int ui, int64_t ix, int64_t n, const iso8211::Field* data) {
        if (tag == "FSPC") {
          if (data != nullptr && data->tag() != "FSPT")
            return Status::Error(kIoError, "fv_s57: FSPC not followed by FSPT in " + id);
          return EditRows(&feat.pointers, ui, ix, n,
                          data ? ReadPointers(*data) : std::vector<S57SpatialPointer>(), id);
        }
        if (tag == "FFPC") {
          if (data != nullptr && data->tag() != "FFPT")
            return Status::Error(kIoError, "fv_s57: FFPC not followed by FFPT in " + id);
          return EditRows(&feat.relations, ui, ix, n,
                          data ? ReadRelations(*data) : std::vector<S57FeatureRelation>(), id);
        }
        return Status::Error(kIoError, "fv_s57: " + tag + " in a feature record, " + id);
      });
}

}  // namespace

Status S57RecordSet::AddBaseRecord(const iso8211::Record& rec, const std::string& path) {
  if (const iso8211::Field* vrid = rec.Find("VRID")) {
    S57VectorRecord v = ReadVector(rec, *vrid);
    const uint64_t key = S57SpatialKey(v.rcnm, v.rcid);
    vectors[key] = std::move(v);
    return Status::Ok();
  }
  if (const iso8211::Field* frid = rec.Find("FRID")) {
    if (frid->Int(0, "RCNM", kS57RcnmFeature) != kS57RcnmFeature)
      return Status::Error(kIoError, "fv_s57: FRID with wrong RCNM in " + path);
    S57FeatureRecord feat = ReadFeature(rec, *frid);
    feature_index[feat.rcid] = features.size();
    features.push_back(std::move(feat));
  }
  return Status::Ok();
}

S57FeatureRecord* S57RecordSet::FindFeature(uint32_t rcid) {
  auto it = feature_index.find(rcid);
  if (it == feature_index.end()) return nullptr;
  S57FeatureRecord& f = features[it->second];
  return f.deleted ? nullptr : &f;
}

Status ApplyS57Update(const std::string& path, S57DatasetInfo* info,
                      S57RecordSet* records) {
  iso8211::Reader reader;
  Status s = reader.Open(path);
  if (!s.ok()) return s;
  const std::string what = fs::path(path).filename().string();

  iso8211::Record rec;
  s = reader.Next(&rec);
  if (!s.ok()) return Status::Error(kIoError, "fv_s57: empty update file " + what);
  const iso8211::Field* dsid = rec.Find("DSID");
  if (dsid == nullptr) return Status::Error(kIoError, "fv_s57: no DSID in " + what);
  const std::string edition = dsid->Text(0, "EDTN");
  const std::string update_number = dsid->Text(0, "UPDN");
  const std::string update_date = dsid->Text(0, "UADT");
  const std::string issue_date = dsid->Text(0, "ISDT");
  // NOAA update files leave UADT blank; ISDT is the update's issue date.
  auto advance = [&] {
    info->update_number = update_number;
    if (!update_date.empty()) info->update_date = update_date;
    if (!issue_date.empty()) info->issue_date = issue_date;
  };
  const int expected = ParseNumber(info->update_number, 0) + 1;
  if (ParseNumber(update_number, -1) != expected)
    return Status::Error(kIoError, "fv_s57: " + what + " is update " + update_number +
                                       ", expected " + std::to_string(expected));
  // EDTN 0 cancels the cell (ENC Product Specification); the file carries no records.
  if (edition == "0") {
    advance();
    info->cancelled = true;
    return Status::Ok();
  }
  if (edition != info->edition)
    return Status::Error(kIoError, "fv_s57: " + what + " updates edition " + edition +
                                       ", the cell is edition " + info->edition);

  UndoLog undo;
  undo.feature_count = records->features.size();
  while (true) {
    s = reader.Next(&rec);
    if (s.code == kNotFound) {
      s = Status::Ok();
      break;
    }
    if (!s.ok()) break;
    if (const iso8211::Field* vrid = rec.Find("VRID")) {
      s = ApplyVectorRecord(rec, *vrid, what, records, &undo);
    } else if (const iso8211::Field* frid = rec.Find("FRID")) {
      s = ApplyFeatureRecord(rec, *frid, what, records, &undo);
    }
    if (!s.ok()) break;
  }
  if (!s.ok()) {
    undo.Restore(records);
    return s;
  }
  advance();
  return Status::Ok();
}

}  // namespace fv
