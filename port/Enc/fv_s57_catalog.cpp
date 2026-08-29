// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_s57_catalog.h"

#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

namespace fv {
namespace {

std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// "ADMARE;NATION;NOBJNM;" -> {ADMARE, NATION, NOBJNM}. Trailing separator and
// empty fields are normal in these tables.
std::vector<std::string> SplitSemicolons(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ';') {
      const std::string t = Trim(cur);
      if (!t.empty()) out.push_back(t);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  const std::string t = Trim(cur);
  if (!t.empty()) out.push_back(t);
  return out;
}

// Census of the delivered table (verified, not assumed): G 218, M 16, C 3
// (C_AGGR / C_ASSO / C_STAC — S-57's COLLECTION objects), $ 5 ($AREAS $LINES
// $CSYMB $COMPS $TEXTS — the CARTOGRAPHIC/presentation pseudo-objects), plus
// 9 rows outside Appendix A that OpenCPN carries with an empty or 'O' class
// (_texto, _extgn, tisdge, ...). Note the C/$ assignment: it is the opposite
// of what the letters suggest at a glance.
S57RecordClass ToRecordClass(const std::string& s) {
  if (s.empty()) return S57RecordClass::kUnknown;
  switch (s[0]) {
    case 'G': return S57RecordClass::kGeo;
    case 'M': return S57RecordClass::kMeta;
    case 'C': return S57RecordClass::kCollection;
    case '$': return S57RecordClass::kCartographic;
    default: return S57RecordClass::kUnknown;
  }
}

S57AttributeType ToAttributeType(const std::string& s) {
  if (s.empty()) return S57AttributeType::kUnknown;
  switch (s[0]) {
    case 'E': return S57AttributeType::kEnumerated;
    case 'L': return S57AttributeType::kList;
    case 'F': return S57AttributeType::kFloat;
    case 'I': return S57AttributeType::kInteger;
    case 'A': return S57AttributeType::kCodedString;
    case 'S': return S57AttributeType::kFreeText;
    default: return S57AttributeType::kUnknown;
  }
}

// Reads a CSV whose first line is a header. `row` is called per data line.
// Blank lines are skipped; a short row is skipped rather than failing the load
// (these tables are third-party data and one malformed row must not cost the
// other 250).
Status ReadCsv(const std::string& path, size_t min_fields,
               const std::function<void(const std::vector<std::string>&)>& row) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot open " + path);
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (first) {  // header
      first = false;
      continue;
    }
    if (Trim(line).empty()) continue;
    const std::vector<std::string> f = ParseCsvLine(line);
    if (f.size() < min_fields) continue;
    row(f);
  }
  return Status::Ok();
}

}  // namespace

std::vector<std::string> ParseCsvLine(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {  // "" escape
          cur.push_back('"');
          ++i;
        } else {
          quoted = false;
        }
      } else {
        cur.push_back(c);
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      out.push_back(Trim(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(Trim(cur));
  return out;
}

Status S57ObjectCatalog::Load(const std::string& data_dir) {
  std::string dir = data_dir;
  // Accept a path to one of the CSVs as well as the directory holding them.
  const size_t slash = dir.find_last_of("/\\");
  if (dir.size() > 4 && dir.compare(dir.size() - 4, 4, ".csv") == 0) {
    dir = (slash == std::string::npos) ? std::string(".") : dir.substr(0, slash);
  }
  if (!dir.empty() && (dir.back() == '/' || dir.back() == '\\')) dir.pop_back();

  classes_.clear();
  class_by_acronym_.clear();
  attributes_.clear();
  attribute_by_acronym_.clear();
  enums_.clear();
  loaded_ = false;

  // Code,ObjectClass,Acronym,Attribute_A,Attribute_B,Attribute_C,Class,Primitives
  Status st = ReadCsv(dir + "/s57objectclasses.csv", 7,
                      [this](const std::vector<std::string>& f) {
                        S57ObjectClass c;
                        c.code = std::atoi(f[0].c_str());
                        c.name = f[1];
                        c.acronym = f[2];
                        c.attributes_a = SplitSemicolons(f[3]);
                        c.attributes_b = SplitSemicolons(f[4]);
                        c.attributes_c = SplitSemicolons(f[5]);
                        c.record_class = ToRecordClass(f[6]);
                        if (f.size() > 7) c.primitives = SplitSemicolons(f[7]);
                        if (c.code == 0 || c.acronym.empty()) return;
                        // Code collisions exist in this table: OpenCPN carries
                        // pseudo-classes outside Appendix A, and `_texto`
                        // reuses 135 (TESARE). FIRST WINS, so the real S-57
                        // class keeps the code; both keep their acronym entry.
                        class_by_acronym_[c.acronym] = c.code;
                        classes_.emplace(c.code, std::move(c));
                      });
  if (!st.ok()) return st;

  // Code,Attribute,Acronym,Attributetype,Class
  st = ReadCsv(dir + "/s57attributes.csv", 5,
               [this](const std::vector<std::string>& f) {
                 S57AttributeDef a;
                 a.code = std::atoi(f[0].c_str());
                 a.name = f[1];
                 a.acronym = f[2];
                 a.type = ToAttributeType(f[3]);
                 a.attribute_class = f[4].empty() ? '\0' : f[4][0];
                 if (a.code == 0 || a.acronym.empty()) return;
                 attribute_by_acronym_[a.acronym] = a.code;
                 attributes_[a.code] = std::move(a);
               });
  if (!st.ok()) return st;

  // Code,ID,Meaning
  st = ReadCsv(dir + "/s57expectedinput.csv", 3,
               [this](const std::vector<std::string>& f) {
                 const int code = std::atoi(f[0].c_str());
                 const int id = std::atoi(f[1].c_str());
                 if (code == 0) return;
                 enums_[static_cast<int64_t>(code) * 100000 + id] = f[2];
               });
  if (!st.ok()) return st;

  if (classes_.empty() || attributes_.empty()) {
    return Status::Error(kIoError, "S-57 catalogue in " + dir + " is empty");
  }
  loaded_ = true;
  return Status::Ok();
}

const S57ObjectClass* S57ObjectCatalog::ObjectClass(int code) const {
  auto it = classes_.find(code);
  return it == classes_.end() ? nullptr : &it->second;
}

const S57ObjectClass* S57ObjectCatalog::ObjectClassByAcronym(
    const std::string& acronym) const {
  auto it = class_by_acronym_.find(acronym);
  return it == class_by_acronym_.end() ? nullptr : ObjectClass(it->second);
}

const S57AttributeDef* S57ObjectCatalog::Attribute(int code) const {
  auto it = attributes_.find(code);
  return it == attributes_.end() ? nullptr : &it->second;
}

const S57AttributeDef* S57ObjectCatalog::AttributeByAcronym(
    const std::string& acronym) const {
  auto it = attribute_by_acronym_.find(acronym);
  return it == attribute_by_acronym_.end() ? nullptr : Attribute(it->second);
}

const std::string* S57ObjectCatalog::EnumeratedValue(int attribute_code,
                                                     int value_id) const {
  auto it = enums_.find(static_cast<int64_t>(attribute_code) * 100000 + value_id);
  return it == enums_.end() ? nullptr : &it->second;
}

std::string S57ObjectCatalog::DescribeValue(int attribute_code,
                                            const std::string& raw) const {
  const S57AttributeDef* def = Attribute(attribute_code);
  if (def == nullptr || raw.empty()) return raw;
  if (def->type != S57AttributeType::kEnumerated &&
      def->type != S57AttributeType::kList) {
    return raw;
  }
  std::string out;
  std::istringstream ss(raw);
  std::string tok;
  while (std::getline(ss, tok, ',')) {
    tok = Trim(tok);
    if (tok.empty()) continue;
    const int id = std::atoi(tok.c_str());
    const std::string* meaning = EnumeratedValue(attribute_code, id);
    if (!out.empty()) out += ", ";
    out += meaning != nullptr ? *meaning : tok + " (undefined)";
  }
  return out.empty() ? raw : out;
}

}  // namespace fv
