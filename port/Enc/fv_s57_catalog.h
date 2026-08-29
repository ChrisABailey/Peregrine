// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// fv_s57_catalog.h — the S-57 Appendix A object/attribute catalogue (ENC phase
// E2, plan section 7). This is the gap E1 deliberately left open: a cell stores
// object classes and attributes as NUMBERS (OBJL 42, ATTL 87), and their
// acronyms are the dispatch key everything above the reader uses — S-52's
// lookup tables are keyed on `DEPARE`, not on 42.
//
// Source: the three CSVs delivered with the presentation library in
// `TestData/enc/` (they are the same tables GDAL and OpenCPN ship, transcribed
// from IHO S-57 Appendix A):
//
//   s57objectclasses.csv   251 rows: code, long name, acronym, attribute sets,
//                          CLASS (G geo / M meta / C cartographic / $ collection),
//                          allowed primitives
//   s57attributes.csv      313 rows: code, long name, acronym, type
//                          (E enumerated / L list / I integer / F float /
//                          A coded string / S free text), class (F/N/S)
//   s57expectedinput.csv  1467 rows: (attribute code, value id) -> meaning —
//                          S-57's equivalent of VPF's INT.VDT, and what gives
//                          ENC a real Describe() in the R1 sense
//
// It is DATA, not code, and it is not guessed at: nothing here is written from
// memory, exactly as E1 refused to. Missing files are an error, not a silent
// empty catalogue, because a chart that silently loses its acronyms would
// simply draw nothing and look like a styling bug.
//
// The catalogue also settles E1's other open item: DSSI splits feature records
// into meta / cartographic / geo / collection, and that split is a property of
// the object class (the `Class` column), so `S57ObjectClass::record_class` is
// what a caller compares against NOMR / NOCR / NOGR / NOLR.

#ifndef FV_S57_CATALOG_H_
#define FV_S57_CATALOG_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "fvkit/geo.h"

namespace fv {

// The `Class` column of s57objectclasses.csv. Read the letters carefully: 'C'
// is the COLLECTION class (C_AGGR / C_ASSO / C_STAC) and '$' the CARTOGRAPHIC
// one ($AREAS / $LINES / $CSYMB / $COMPS / $TEXTS), which is the opposite of
// what the initials suggest.
enum class S57RecordClass {
  kGeo = 0,        // 'G' — a real charted object (218 rows)
  kMeta,           // 'M' — M_* coverage/quality metadata (16)
  kCollection,     // 'C' — C_AGGR / C_ASSO, no geometry of their own (3)
  kCartographic,   // '$' — presentation pseudo-objects (5)
  kUnknown,        // '' / 'O' — 9 OpenCPN rows outside Appendix A
};

// The `Attributetype` column of s57attributes.csv.
enum class S57AttributeType {
  kEnumerated = 0,  // 'E' — one value id, decoded via expected-input
  kList,            // 'L' — comma-separated value ids
  kFloat,           // 'F'
  kInteger,         // 'I'
  kCodedString,     // 'A'
  kFreeText,        // 'S'
  kUnknown,
};

struct S57ObjectClass {
  int code = 0;               // OBJL
  std::string acronym;        // e.g. "DEPARE"
  std::string name;           // e.g. "Depth area"
  S57RecordClass record_class = S57RecordClass::kUnknown;
  std::vector<std::string> primitives;      // "Point", "Line", "Area"
  std::vector<std::string> attributes_a;    // individual
  std::vector<std::string> attributes_b;    // descriptive
  std::vector<std::string> attributes_c;    // spatial / metadata
};

struct S57AttributeDef {
  int code = 0;               // ATTL
  std::string acronym;        // e.g. "DRVAL1"
  std::string name;           // e.g. "Depth range value 1"
  S57AttributeType type = S57AttributeType::kUnknown;
  char attribute_class = '\0';  // 'F' feature, 'N' national, 'S' spatial
};

class S57ObjectCatalog {
 public:
  // `data_dir` holds the three CSVs (TestData/enc in this tree). A path to any
  // one of them, or to the directory, both work.
  Status Load(const std::string& data_dir);
  bool is_loaded() const { return loaded_; }

  // nullptr when the code/acronym is not in the catalogue — which happens for
  // real: producers emit codes outside Appendix A, and a caller that shows the
  // number is more honest than one that invents an acronym.
  const S57ObjectClass* ObjectClass(int code) const;
  const S57ObjectClass* ObjectClassByAcronym(const std::string& acronym) const;
  const S57AttributeDef* Attribute(int code) const;
  const S57AttributeDef* AttributeByAcronym(const std::string& acronym) const;

  // Decoded meaning of one enumerated value, or nullptr.
  const std::string* EnumeratedValue(int attribute_code, int value_id) const;

  // Human-readable rendering of a raw ATVL string for `attribute_code`:
  // enumerated codes become their meaning, lists become comma-joined meanings,
  // everything else comes back unchanged. An unknown id is rendered as
  // "<id> (undefined)" rather than dropped — an unexpected value in a chart is
  // information, not noise.
  std::string DescribeValue(int attribute_code, const std::string& raw) const;

  size_t object_class_count() const { return classes_.size(); }
  size_t attribute_count() const { return attributes_.size(); }
  size_t enumerated_value_count() const { return enums_.size(); }

 private:
  bool loaded_ = false;
  std::unordered_map<int, S57ObjectClass> classes_;
  std::unordered_map<std::string, int> class_by_acronym_;
  std::unordered_map<int, S57AttributeDef> attributes_;
  std::unordered_map<std::string, int> attribute_by_acronym_;
  // key = attribute_code * 100000 + value_id; ids are small and codes < 100000.
  std::unordered_map<int64_t, std::string> enums_;
};

// Splits one CSV line into fields, honouring "quoted, fields" and doubled ""
// escapes. Exposed because the PresLib tests and any future CSV table (S-57
// has several) want the same one, and it is worth testing on its own.
std::vector<std::string> ParseCsvLine(const std::string& line);

}  // namespace fv

#endif  // FV_S57_CATALOG_H_
