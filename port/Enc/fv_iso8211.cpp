// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fv_iso8211.cpp — see fv_iso8211.h.

#include "fv_iso8211.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fv {
namespace iso8211 {
namespace {

// ISO 8211 leaders and directories are ASCII digits with no terminator, so
// every numeric field is a fixed-width unterminated digit run. Blanks read as
// zero: a data record leaves the leader's field-control length (and, in these
// NOAA cells, the interchange level) blank because those apply to the DDR only.
bool ReadDigits(const uint8_t* p, size_t n, size_t* out) {
  size_t v = 0;
  for (size_t i = 0; i < n; ++i) {
    if (p[i] == ' ') continue;
    if (p[i] < '0' || p[i] > '9') return false;
    v = v * 10 + static_cast<size_t>(p[i] - '0');
  }
  *out = v;
  return true;
}

Status Fail(int code, const std::string& msg) { return Status::Error(code, msg); }

// Splits "a,b(3),c" on top-level commas.
std::vector<std::string> SplitTopLevel(const std::string& s) {
  std::vector<std::string> parts;
  int depth = 0;
  std::string cur;
  for (char c : s) {
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (c == ',' && depth == 0) {
      parts.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) parts.push_back(cur);
  return parts;
}

// One format-control token: [repeat] type [(width)].
Status ParseToken(const std::string& tok, int* repeat, SubfieldSpec* spec) {
  size_t i = 0;
  std::string digits;
  while (i < tok.size() && tok[i] >= '0' && tok[i] <= '9') digits.push_back(tok[i++]);
  if (i < tok.size() && tok[i] == '(')
    return Fail(kUnsupported,
                "iso8211: nested format group not supported: " + tok);
  if (i >= tok.size()) return Fail(kIoError, "iso8211: empty format token");
  *repeat = digits.empty() ? 1 : atoi(digits.c_str());
  if (*repeat <= 0) return Fail(kIoError, "iso8211: bad repeat in " + tok);

  const char type = tok[i++];
  std::string width;
  if (i < tok.size()) {
    if (type == 'b') {
      // "b12" — first digit is the binary kind, second the width in bytes.
      if (tok.size() - i < 2)
        return Fail(kIoError, "iso8211: short binary format " + tok);
      spec->binary_kind = tok[i] - '0';
      spec->width = tok[i + 1] - '0';
      i += 2;
      if (spec->binary_kind != 1 && spec->binary_kind != 2 &&
          spec->binary_kind != 4 && spec->binary_kind != 5)
        return Fail(kUnsupported, "iso8211: binary kind not supported: " + tok);
      if (spec->width < 1 || spec->width > 8)
        return Fail(kIoError, "iso8211: bad binary width in " + tok);
    } else if (tok[i] == '(') {
      ++i;
      while (i < tok.size() && tok[i] != ')') width.push_back(tok[i++]);
      if (i >= tok.size()) return Fail(kIoError, "iso8211: unclosed width in " + tok);
      ++i;
      spec->width = atoi(width.c_str());
    }
  }
  if (i != tok.size()) return Fail(kIoError, "iso8211: trailing junk in " + tok);

  spec->format = type;
  switch (type) {
    case 'A':
    case 'I':
    case 'R':
    case 'S':
      break;  // width 0 means UT-delimited
    case 'B':
      // ISO 8211 states bit-string widths in BITS.
      if (spec->width % 8 != 0)
        return Fail(kUnsupported, "iso8211: sub-byte bit string: " + tok);
      spec->width /= 8;
      break;
    case 'b':
      break;
    default:
      return Fail(kUnsupported, std::string("iso8211: format '") + type +
                                    "' not supported (" + tok + ")");
  }
  return Status::Ok();
}

}  // namespace

int64_t Value::AsInt() const {
  switch (kind) {
    case ValueKind::kInt:
      return integer;
    case ValueKind::kReal:
      return static_cast<int64_t>(real);
    case ValueKind::kText:
      return text.empty() ? 0 : atoll(text.c_str());
    case ValueKind::kBytes:
      break;
  }
  return 0;
}

double Value::AsDouble() const {
  switch (kind) {
    case ValueKind::kInt:
      return static_cast<double>(integer);
    case ValueKind::kReal:
      return real;
    case ValueKind::kText:
      return text.empty() ? 0.0 : atof(text.c_str());
    case ValueKind::kBytes:
      break;
  }
  return 0.0;
}

std::string Value::AsText() const {
  char buf[64];
  // Anything that arrived as characters — A, and the ASCII numerics I and R —
  // shows in its raw form: S-57's STED is R(4) "03.1", not 3.1.
  if (kind == ValueKind::kText || !text.empty()) return text;
  switch (kind) {
    case ValueKind::kText:
    case ValueKind::kInt:
      snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(integer));
      return buf;
    case ValueKind::kReal:
      snprintf(buf, sizeof(buf), "%g", real);
      return buf;
    case ValueKind::kBytes: {
      std::string hex;
      hex.reserve(bytes.size() * 2);
      for (uint8_t b : bytes) {
        snprintf(buf, sizeof(buf), "%02x", b);
        hex += buf;
      }
      return hex;
    }
  }
  return {};
}

const Value* Field::Get(size_t row, const std::string& label) const {
  if (row >= rows_.size()) return nullptr;
  for (size_t i = 0; i < def_->subfields.size() && i < rows_[row].size(); ++i)
    if (def_->subfields[i].label == label) return &rows_[row][i];
  return nullptr;
}

int64_t Field::Int(size_t row, const std::string& label, int64_t missing) const {
  const Value* v = Get(row, label);
  if (v == nullptr || v->null) return missing;
  return v->AsInt();
}

double Field::Real(size_t row, const std::string& label, double missing) const {
  const Value* v = Get(row, label);
  if (v == nullptr || v->null) return missing;
  return v->AsDouble();
}

std::string Field::Text(size_t row, const std::string& label) const {
  const Value* v = Get(row, label);
  return v == nullptr ? std::string() : v->AsText();
}

const Field* Record::Find(const std::string& tag) const {
  for (const Field& f : fields_)
    if (f.tag() == tag) return &f;
  return nullptr;
}

std::vector<const Field*> Record::FindAll(const std::string& tag) const {
  std::vector<const Field*> out;
  for (const Field& f : fields_)
    if (f.tag() == tag) out.push_back(&f);
  return out;
}

Status ParseFormatControls(const std::string& format_controls,
                           const std::string& array_descriptor,
                           std::vector<SubfieldSpec>* out) {
  out->clear();
  std::string fmt = format_controls;
  if (fmt.size() < 2 || fmt.front() != '(' || fmt.back() != ')')
    return Fail(kIoError, "iso8211: format controls not parenthesised: " + fmt);
  fmt = fmt.substr(1, fmt.size() - 2);

  // Labels: '!'-separated, a leading '*' marks the field as repeating (handled
  // by the caller, which owns the FieldDefinition).
  std::string labels_str = array_descriptor;
  if (!labels_str.empty() && labels_str[0] == '*') labels_str.erase(0, 1);
  std::vector<std::string> labels;
  if (!labels_str.empty()) {
    size_t start = 0;
    while (true) {
      size_t bang = labels_str.find('!', start);
      labels.push_back(labels_str.substr(
          start, bang == std::string::npos ? std::string::npos : bang - start));
      if (bang == std::string::npos) break;
      start = bang + 1;
    }
  }

  for (const std::string& tok : SplitTopLevel(fmt)) {
    int repeat = 1;
    SubfieldSpec spec;
    Status s = ParseToken(tok, &repeat, &spec);
    if (!s.ok()) return s;
    for (int r = 0; r < repeat; ++r) out->push_back(spec);
  }

  if (labels.empty()) {
    // Fields such as ISO 8211's own "0001" record identifier carry no labels;
    // number them so a caller can still reach them positionally.
    for (size_t i = 0; i < out->size(); ++i)
      (*out)[i].label = std::to_string(i + 1);
    return Status::Ok();
  }
  if (labels.size() != out->size())
    return Fail(kIoError, "iso8211: " + std::to_string(labels.size()) +
                              " subfield labels but " +
                              std::to_string(out->size()) + " formats (" + fmt +
                              ")");
  for (size_t i = 0; i < labels.size(); ++i) (*out)[i].label = labels[i];
  return Status::Ok();
}

Status Reader::Open(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (f == nullptr) return Fail(kIoError, "iso8211: cannot open " + path);
  std::vector<uint8_t> data;
  uint8_t buf[65536];
  size_t got;
  while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
    data.insert(data.end(), buf, buf + got);
  const bool bad = ferror(f) != 0;
  fclose(f);
  if (bad) return Fail(kIoError, "iso8211: read error on " + path);
  return OpenMemory(std::move(data), path);
}

Status Reader::OpenMemory(std::vector<uint8_t> data, const std::string& name) {
  data_ = std::move(data);
  name_ = name;
  pos_ = 0;
  definitions_.clear();
  if (data_.size() < 24) return Fail(kIoError, "iso8211: file too short: " + name);
  return ParseDdr();
}

Status Reader::ParseLeader(size_t at, Leader* out) const {
  if (at + 24 > data_.size()) return Fail(kIoError, "iso8211: truncated leader");
  const uint8_t* p = data_.data() + at;
  size_t len = 0, base = 0, fcl = 0, sfl = 0, sfp = 0, sft = 0;
  if (!ReadDigits(p, 5, &len) || !ReadDigits(p + 12, 5, &base) ||
      !ReadDigits(p + 10, 2, &fcl) || !ReadDigits(p + 20, 1, &sfl) ||
      !ReadDigits(p + 21, 1, &sfp) || !ReadDigits(p + 23, 1, &sft))
    return Fail(kIoError, "iso8211: non-numeric leader in " + name_);
  if (len < 24 || at + len > data_.size())
    return Fail(kIoError, "iso8211: record length " + std::to_string(len) +
                              " overruns " + name_);
  if (base < 24 || base > len)
    return Fail(kIoError, "iso8211: bad base address in " + name_);
  if (sfl == 0 || sfp == 0 || sft == 0)
    return Fail(kIoError, "iso8211: zero entry-map width in " + name_);
  out->record_length = len;
  out->base_address = base;
  out->field_control_length = static_cast<int>(fcl);
  out->size_field_length = static_cast<int>(sfl);
  out->size_field_position = static_cast<int>(sfp);
  out->size_field_tag = static_cast<int>(sft);
  out->leader_id = static_cast<char>(p[6]);
  out->interchange_level = p[5] >= '0' && p[5] <= '9' ? p[5] - '0' : 0;
  return Status::Ok();
}

Status Reader::ParseDdr() {
  Leader ldr;
  Status s = ParseLeader(0, &ldr);
  if (!s.ok()) return s;
  if (ldr.leader_id != 'L')
    return Fail(kIoError, "iso8211: first record is not a DDR (leader id '" +
                              std::string(1, ldr.leader_id) + "') in " + name_);
  interchange_level_ = ldr.interchange_level;

  const uint8_t* rec = data_.data();
  const size_t entry_size =
      static_cast<size_t>(ldr.size_field_tag) + ldr.size_field_length +
      ldr.size_field_position;

  size_t i = 24;
  while (i + entry_size <= ldr.base_address && rec[i] != kFieldTerminator) {
    const std::string tag(reinterpret_cast<const char*>(rec + i),
                          ldr.size_field_tag);
    size_t flen = 0, fpos = 0;
    if (!ReadDigits(rec + i + ldr.size_field_tag, ldr.size_field_length, &flen) ||
        !ReadDigits(rec + i + ldr.size_field_tag + ldr.size_field_length,
                    ldr.size_field_position, &fpos))
      return Fail(kIoError, "iso8211: bad DDR directory entry in " + name_);
    i += entry_size;
    if (ldr.base_address + fpos + flen > ldr.record_length)
      return Fail(kIoError, "iso8211: DDR field " + tag + " overruns record");
    if (tag == "0000") continue;  // field-pair tree; S-57 does not need it

    const uint8_t* body = rec + ldr.base_address + fpos;
    size_t blen = flen;
    while (blen > 0 && body[blen - 1] == kFieldTerminator) --blen;
    if (static_cast<int>(blen) < ldr.field_control_length)
      return Fail(kIoError, "iso8211: DDR field " + tag + " shorter than its "
                            "field controls");

    // Field controls, then name UT array-descriptor UT format-controls.
    size_t at = static_cast<size_t>(ldr.field_control_length);
    std::string parts[3];
    for (int k = 0; k < 3; ++k) {
      size_t end = at;
      while (end < blen && body[end] != kUnitTerminator) ++end;
      parts[k].assign(reinterpret_cast<const char*>(body + at), end - at);
      at = end < blen ? end + 1 : blen;
    }

    FieldDefinition def;
    def.tag = tag;
    def.name = parts[0];
    def.repeating = !parts[1].empty() && parts[1][0] == '*';
    def.format_controls = parts[2];
    if (parts[2].empty()) {
      // A field with no format controls carries a single UT-delimited string.
      SubfieldSpec spec;
      spec.format = 'A';
      spec.label = parts[1].empty() ? "1" : parts[1];
      def.subfields.push_back(spec);
    } else {
      s = ParseFormatControls(parts[2], parts[1], &def.subfields);
      if (!s.ok())
        return Status::Error(s.code, s.message + " [field " + tag + " of " + name_ + "]");
    }
    definitions_[tag] = def;
  }

  pos_ = ldr.record_length;
  return Status::Ok();
}

Status Reader::DecodeField(const FieldDefinition& def, const uint8_t* p,
                           size_t n, Field* out) const {
  out->def_ = &def;
  out->rows_.clear();
  // A field's data ends with FT; drop it so subfield scanning sees only data.
  while (n > 0 && p[n - 1] == kFieldTerminator) --n;

  size_t at = 0;
  do {
    const size_t row_start = at;
    std::vector<Value> row;
    row.reserve(def.subfields.size());
    for (const SubfieldSpec& spec : def.subfields) {
      Value v;
      if (spec.format == 'b') {
        if (at + static_cast<size_t>(spec.width) > n)
          return Fail(kIoError, "iso8211: field " + def.tag + " truncated in " + name_);
        const uint8_t* q = p + at;
        bool all_ones = true;
        for (int k = 0; k < spec.width; ++k)
          if (q[k] != 0xff) all_ones = false;
        if (spec.binary_kind == 4 || spec.binary_kind == 5) {
          v.kind = ValueKind::kReal;
          if (spec.width == 4) {
            float f;
            memcpy(&f, q, 4);
            v.real = f;
          } else if (spec.width == 8) {
            double d;
            memcpy(&d, q, 8);
            v.real = d;
          } else {
            return Fail(kUnsupported, "iso8211: float width " +
                                          std::to_string(spec.width) + " in " +
                                          def.tag);
          }
        } else {
          v.kind = ValueKind::kInt;
          uint64_t u = 0;
          for (int k = spec.width - 1; k >= 0; --k)  // S-57: LSB first
            u = (u << 8) | q[k];
          if (spec.binary_kind == 2 && spec.width < 8 &&
              (u & (1ull << (spec.width * 8 - 1))) != 0)
            u |= ~((1ull << (spec.width * 8)) - 1);  // sign-extend
          v.integer = static_cast<int64_t>(u);
        }
        v.null = all_ones;
        at += spec.width;
      } else if (spec.format == 'B') {
        if (at + static_cast<size_t>(spec.width) > n)
          return Fail(kIoError, "iso8211: field " + def.tag + " truncated in " + name_);
        v.kind = ValueKind::kBytes;
        v.bytes.assign(p + at, p + at + spec.width);
        at += spec.width;
      } else {
        size_t end;
        if (spec.width > 0) {
          end = at + static_cast<size_t>(spec.width);
          if (end > n)
            return Fail(kIoError, "iso8211: field " + def.tag + " truncated in " + name_);
        } else {
          end = at;
          while (end < n && p[end] != kUnitTerminator &&
                 p[end] != kFieldTerminator)
            ++end;
        }
        const std::string raw(reinterpret_cast<const char*>(p + at), end - at);
        at = end;
        if (spec.width == 0 && at < n) ++at;  // consume the UT
        if (spec.format == 'I') {
          v.kind = ValueKind::kInt;
          v.integer = raw.empty() ? 0 : atoll(raw.c_str());
          v.text = raw;
        } else if (spec.format == 'R') {
          v.kind = ValueKind::kReal;
          v.real = raw.empty() ? 0.0 : atof(raw.c_str());
          v.text = raw;
        } else {
          v.kind = ValueKind::kText;
          v.text = raw;
        }
        // Trailing-blank trim: fixed-width ASCII subfields are blank padded.
        if (spec.width > 0) {
          size_t last = v.text.find_last_not_of(' ');
          v.text = last == std::string::npos ? std::string()
                                             : v.text.substr(0, last + 1);
        }
        v.null = v.text.empty();
      }
      row.push_back(std::move(v));
    }
    out->rows_.push_back(std::move(row));
    if (!def.repeating) break;
    if (at <= row_start) break;  // zero-width row: stop rather than spin
  } while (at < n);
  return Status::Ok();
}

Status Reader::Next(Record* out) {
  out->fields_.clear();
  if (eof()) return Fail(kNotFound, "iso8211: end of " + name_);

  Leader ldr;
  Status s = ParseLeader(pos_, &ldr);
  if (!s.ok()) return s;
  const uint8_t* rec = data_.data() + pos_;
  const size_t entry_size =
      static_cast<size_t>(ldr.size_field_tag) + ldr.size_field_length +
      ldr.size_field_position;

  size_t i = 24;
  while (i + entry_size <= ldr.base_address && rec[i] != kFieldTerminator) {
    const std::string tag(reinterpret_cast<const char*>(rec + i),
                          ldr.size_field_tag);
    size_t flen = 0, fpos = 0;
    if (!ReadDigits(rec + i + ldr.size_field_tag, ldr.size_field_length, &flen) ||
        !ReadDigits(rec + i + ldr.size_field_tag + ldr.size_field_length,
                    ldr.size_field_position, &fpos))
      return Fail(kIoError, "iso8211: bad directory entry in " + name_);
    i += entry_size;
    if (ldr.base_address + fpos + flen > ldr.record_length)
      return Fail(kIoError, "iso8211: field " + tag + " overruns record in " + name_);

    auto it = definitions_.find(tag);
    if (it == definitions_.end())
      return Fail(kIoError, "iso8211: field " + tag + " has no DDR definition in " + name_);
    Field field;
    s = DecodeField(it->second, rec + ldr.base_address + fpos, flen, &field);
    if (!s.ok()) return s;
    out->fields_.push_back(std::move(field));
  }

  pos_ += ldr.record_length;
  return Status::Ok();
}

const FieldDefinition* Reader::Definition(const std::string& tag) const {
  auto it = definitions_.find(tag);
  return it == definitions_.end() ? nullptr : &it->second;
}

}  // namespace iso8211
}  // namespace fv
