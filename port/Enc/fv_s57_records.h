// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/**
 * fv_s57_records.h — an S-57 cell's feature and spatial records in an
 * editable form, and the update applier (S-57 3.1 Part 3 §8) that edits them.
 *
 * `S57Cell` reads a base cell into an `S57RecordSet`, applies its update files
 * with `ApplyS57Update`, then assembles geometry from the result. Values stay
 * as stored (coordinates unscaled, pointers in record order) because update
 * instructions address them by position.
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "fv_iso8211.h"
#include "fv_s57.h"

namespace fv {

/** S-57 record names (RCNM). */
constexpr int kS57RcnmFeature = 100;
constexpr int kS57RcnmIsolatedNode = 110;
constexpr int kS57RcnmConnectedNode = 120;
constexpr int kS57RcnmEdge = 130;
constexpr int kS57RcnmFace = 140;

/** A spatial record's key: RCNM in the high bits, RCID below, as NAME encodes it. */
inline uint64_t S57SpatialKey(int rcnm, uint32_t rcid) {
  return (static_cast<uint64_t>(rcnm) << 32) | rcid;
}

/** One VRPT or FSPT row. 255 is the S-57 missing value for each enum. */
struct S57SpatialPointer {
  uint64_t name = 0;  // spatial-record key of the target
  int orientation = 255;  // ORNT
  int usage = 255;        // USAG
  int topology = 255;     // TOPI (VRPT only)
  int mask = 255;         // MASK
};

/** One SG2D or SG3D row before COMF/SOMF scaling. */
struct S57RawCoordinate {
  int64_t y = 0;
  int64_t x = 0;
  int64_t z = 0;  // VE3D; SG3D only
  bool has_z = false;
};

/** A vector record: isolated node, connected node, edge or face. */
struct S57VectorRecord {
  int rcnm = 0;
  uint32_t rcid = 0;
  int version = 0;  // RVER
  std::vector<S57Attribute> attributes;  // ATTV
  std::vector<S57SpatialPointer> pointers;  // VRPT, all occurrences in order
  std::vector<S57RawCoordinate> coordinates;  // SG2D/SG3D, in order
};

/** A feature record before geometry assembly. */
struct S57FeatureRecord {
  uint32_t rcid = 0;
  int primitive = 255;  // PRIM
  int group = 0;        // GRUP
  int object_class = 0;  // OBJL
  int version = 0;      // RVER
  S57ObjectId object_id;
  std::vector<S57Attribute> attributes;  // ATTF then NATF
  std::vector<S57FeatureRelation> relations;  // FFPT
  std::vector<S57SpatialPointer> pointers;  // FSPT, all occurrences in order
  bool deleted = false;  // removed by an update; kept so file order survives
};

/** The records of one cell. Features keep file order; updates append inserts. */
struct S57RecordSet {
  std::unordered_map<uint64_t, S57VectorRecord> vectors;
  std::vector<S57FeatureRecord> features;
  std::unordered_map<uint32_t, size_t> feature_index;  // RCID -> features[]

  /** Adds a base-cell VRID or FRID record; other records are ignored. */
  Status AddBaseRecord(const iso8211::Record& rec, const std::string& path);

  /** nullptr when absent or deleted. */
  S57FeatureRecord* FindFeature(uint32_t rcid);
};

/**
 * Applies one update file to `records`. The update's EDTN must equal
 * `info->edition` and its UPDN must be one more than `info->update_number`.
 * On success `info` carries the new UPDN, and the update's ISDT (and UADT
 * when set) as the cell's issue and update dates; an update with EDTN 0 is a
 * cancellation and sets `info->cancelled` instead. On any error `records` and
 * `info` are left exactly as they were.
 */
Status ApplyS57Update(const std::string& path, S57DatasetInfo* info,
                      S57RecordSet* records);

}  // namespace fv
