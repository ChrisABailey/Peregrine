// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_iso8211.h — ISO/IEC 8211 data-descriptive-file reader (ENC phase E1).
//
// ISO 8211 is the record container S-57 rides in; it is also the container of
// an ENC exchange set's CATALOG.031, so the two parse with the same code (see
// port/vpf-geosym-plan.md section 7). Nothing here knows about charts: a file
// is a sequence of records, a record is a set of fields, a field is one or
// more rows of labelled subfields whose types come from the file's Data
// Descriptive Record (DDR).
//
// Deliberately NOT a port of Applications/GeoRect/adrg/iso.cpp: that reader is
// the ADRG-era C library (fixed-size global tables, ASCII subfields only, no
// binary formats), and S-57 is almost entirely binary subfields. Plan section 7
// called for a spec-driven reader; this is it.
//
// Byte order: ISO 8211 leaves binary subfield byte order to the application
// specification. S-57 Part 3 mandates LSB-first, which is the default here.
//
// Missing values: S-57 encodes a missing binary subfield as all bits set (a
// b12 null is 0xFFFF, not 0). Decoded values carry `null` so a caller can tell
// "absent" from "zero" — the values themselves stay verbatim.

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "fvkit/geo.h"  // fv::Status (contract D3)

namespace fv {
namespace iso8211 {

// ISO 8211 delimiters.
constexpr uint8_t kUnitTerminator = 0x1f;   // UT, between subfields
constexpr uint8_t kFieldTerminator = 0x1e;  // FT, at the end of a field

enum class ValueKind {
  kInt,   // binary integer (b1x/b2x) or ASCII integer (I)
  kReal,  // binary float (b4x/b5x) or ASCII real (R)
  kText,  // A (and S, treated as text)
  kBytes  // B — bit string, kept verbatim (S-57's NAME/LNAM keys)
};

// One decoded subfield.
struct Value {
  ValueKind kind = ValueKind::kText;
  bool null = false;  // S-57 missing value: every raw byte was 0xFF, or empty text
  int64_t integer = 0;
  double real = 0.0;
  std::string text;
  std::vector<uint8_t> bytes;

  int64_t AsInt() const;
  double AsDouble() const;
  // Text for any kind (numbers are formatted) — identify/debug output.
  std::string AsText() const;
};

// A subfield's label plus the format control that decodes it.
struct SubfieldSpec {
  std::string label;
  char format = 'A';           // 'A','I','R','S','B','b'
  int binary_kind = 0;         // 'b' only: 1 unsigned, 2 signed, 4 float, 5 double
  int width = 0;               // bytes; 0 = variable, delimited by UT/FT/end
};

// A field's definition, from the DDR.
struct FieldDefinition {
  std::string tag;
  std::string name;             // human-readable, e.g. "2-D coordinate field"
  bool repeating = false;       // array descriptor began with '*'
  std::vector<SubfieldSpec> subfields;
  std::string format_controls;  // verbatim, for diagnostics
};

// One occurrence of a field inside a data record. A repeating field holds one
// row per repetition (SG2D's coordinate array is 1 field, N rows).
class Field {
 public:
  const std::string& tag() const { return def_->tag; }
  const FieldDefinition& def() const { return *def_; }
  size_t row_count() const { return rows_.size(); }
  const std::vector<Value>& row(size_t i) const { return rows_[i]; }

  // nullptr when the label is not in this field.
  const Value* Get(size_t row, const std::string& label) const;
  int64_t Int(size_t row, const std::string& label, int64_t missing = 0) const;
  double Real(size_t row, const std::string& label, double missing = 0.0) const;
  std::string Text(size_t row, const std::string& label) const;

 private:
  friend class Reader;
  const FieldDefinition* def_ = nullptr;
  std::vector<std::vector<Value>> rows_;
};

// One data record (DR).
class Record {
 public:
  const std::vector<Field>& fields() const { return fields_; }
  const Field* Find(const std::string& tag) const;  // first occurrence
  std::vector<const Field*> FindAll(const std::string& tag) const;
  bool Has(const std::string& tag) const { return Find(tag) != nullptr; }

 private:
  friend class Reader;
  std::vector<Field> fields_;
};

// Sequential reader. The whole file is read into memory (ENC cells are under a
// few MB; the catalogue is a few KB) so records can hand out decoded values
// without any borrowed-buffer lifetime rules.
class Reader {
 public:
  Status Open(const std::string& path);
  // For tests and embedded blobs; `name` is used in error messages only.
  Status OpenMemory(std::vector<uint8_t> data, const std::string& name);

  bool is_open() const { return !data_.empty(); }
  bool eof() const { return pos_ >= data_.size(); }
  const std::string& name() const { return name_; }
  int interchange_level() const { return interchange_level_; }

  // Reads the next record. Returns kNotFound at end of file.
  Status Next(Record* out);

  const std::map<std::string, FieldDefinition>& definitions() const {
    return definitions_;
  }
  const FieldDefinition* Definition(const std::string& tag) const;

 private:
  struct Leader {
    size_t record_length = 0;
    size_t base_address = 0;
    int size_field_length = 0;
    int size_field_position = 0;
    int size_field_tag = 4;
    int field_control_length = 0;
    char leader_id = ' ';
    int interchange_level = 0;
  };

  Status ParseLeader(size_t at, Leader* out) const;
  Status ParseDdr();
  Status DecodeField(const FieldDefinition& def, const uint8_t* p, size_t n,
                     Field* out) const;

  std::vector<uint8_t> data_;
  std::string name_;
  size_t pos_ = 0;  // offset of the next record
  int interchange_level_ = 0;
  std::map<std::string, FieldDefinition> definitions_;
};

// Exposed for testing: expands an ISO 8211 format-control string such as
// "(b11,b14,2b11,3A,2A(8),R(4),A)" against a '!'-separated subfield label list.
// Nested groups ("3(A,I)") are reported as unsupported rather than guessed at.
Status ParseFormatControls(const std::string& format_controls,
                           const std::string& array_descriptor,
                           std::vector<SubfieldSpec>* out);

}  // namespace iso8211
}  // namespace fv
