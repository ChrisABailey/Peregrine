// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.
#include "fvkit/feature/value_text.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fv {

namespace {

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string FormatNumber(double d) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.10g", d);
  return buf;
}

/// `n` digits at `s[at]`, as a number; -1 when any is not a digit.
int Digits(const std::string& s, size_t at, size_t n) {
  if (at + n > s.size()) return -1;
  int v = 0;
  for (size_t i = at; i < at + n; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return -1;
    v = v * 10 + (s[i] - '0');
  }
  return v;
}

bool ValidDate(const std::string& s) {
  if (s.size() < 10 || s[4] != '-' || s[7] != '-') return false;
  const int y = Digits(s, 0, 4), m = Digits(s, 5, 2), d = Digits(s, 8, 2);
  if (y < 0 || m < 1 || m > 12 || d < 1) return false;
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return d <= kDays[m - 1] + (m == 2 && leap ? 1 : 0);
}

/// hh:mm[:ss[.fff]][Z|±hh:mm] starting at `at`, running to the end.
bool ValidTime(const std::string& s, size_t at) {
  const int h = Digits(s, at, 2), mi = Digits(s, at + 3, 2);
  if (h < 0 || h > 23 || mi < 0 || mi > 59 || s.size() < at + 5 || s[at + 2] != ':') return false;
  size_t i = at + 5;
  if (i < s.size() && s[i] == ':') {
    const int sec = Digits(s, i + 1, 2);
    if (sec < 0 || sec > 60) return false;  // 60: a leap second
    i += 3;
    if (i < s.size() && s[i] == '.') {
      size_t j = i + 1;
      while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
      if (j == i + 1) return false;
      i = j;
    }
  }
  if (i == s.size()) return true;
  if (s[i] == 'Z' && i + 1 == s.size()) return true;
  if (s[i] != '+' && s[i] != '-') return false;
  const int oh = Digits(s, i + 1, 2), om = Digits(s, i + 4, 2);
  return oh >= 0 && oh <= 14 && om >= 0 && om <= 59 && s[i + 3] == ':' && i + 6 == s.size();
}

Status Bad(const FieldDef& field, const std::string& what) {
  return Status::Error(kInvalidArg, "\"" + field.DisplayTitle() + "\" " + what);
}

}  // namespace

std::string FormatFieldValue(const FieldDef& field, const FieldValue& value) {
  if (field.role == FieldRole::kEnumeration) {
    std::string key;
    if (const auto* s = std::get_if<std::string>(&value)) key = *s;
    else if (const auto* i = std::get_if<int64_t>(&value)) key = std::to_string(*i);
    for (const FieldChoice& c : field.choices)
      if (c.value == key && !key.empty()) return c.label.empty() ? c.value : c.label;
  }
  if (const auto* s = std::get_if<std::string>(&value)) return *s;
  if (const auto* i = std::get_if<int64_t>(&value)) return std::to_string(*i);
  if (const auto* b = std::get_if<bool>(&value)) return *b ? "Yes" : "No";
  if (const auto* d = std::get_if<double>(&value)) return FormatNumber(*d);
  return "";
}

Status ParseFieldValue(const FieldDef& field, const std::string& typed, FieldValue* out) {
  std::string text = Trim(typed);
  if (text.empty()) {
    *out = std::monostate{};
    return Status::Ok();
  }
  if (field.role == FieldRole::kEnumeration) {
    for (const FieldChoice& c : field.choices)
      if (text == c.value || (!c.label.empty() && text == c.label)) {
        text = c.value;
        break;
      }
  }
  switch (field.type) {
    case FieldType::kText:
      *out = field.role == FieldRole::kMultilineText ? typed : text;
      return Status::Ok();
    case FieldType::kInteger: {
      errno = 0;
      char* end = nullptr;
      const long long v = std::strtoll(text.c_str(), &end, 10);
      if (errno != 0 || end == text.c_str() || *end != '\0') return Bad(field, "needs a whole number");
      *out = static_cast<int64_t>(v);
      return Status::Ok();
    }
    case FieldType::kReal: {
      char* end = nullptr;
      const double v = std::strtod(text.c_str(), &end);
      if (end == text.c_str() || *end != '\0' || !std::isfinite(v)) return Bad(field, "needs a number");
      *out = v;
      return Status::Ok();
    }
    case FieldType::kBoolean: {
      const std::string t = Lower(text);
      if (t == "yes" || t == "true" || t == "1") *out = true;
      else if (t == "no" || t == "false" || t == "0") *out = false;
      else return Bad(field, "needs yes or no");
      return Status::Ok();
    }
    case FieldType::kDate:
      if (text.size() != 10 || !ValidDate(text)) return Bad(field, "needs a date as YYYY-MM-DD");
      *out = text;
      return Status::Ok();
    case FieldType::kDateTime:
      if (text.size() > 10 && text[10] == ' ') text[10] = 'T';
      if (text.size() < 16 || !ValidDate(text) || text[10] != 'T' || !ValidTime(text, 11))
        return Bad(field, "needs a date and time as YYYY-MM-DDThh:mm");
      *out = text;
      return Status::Ok();
    case FieldType::kBlob:
      break;
  }
  return Status::Error(kUnsupported, "\"" + field.DisplayTitle() + "\" holds binary data and cannot be typed");
}

std::string FieldActionUrl(const FieldDef& field, const std::string& typed) {
  const std::string text = Trim(typed);
  if (text.empty()) return "";
  switch (field.role) {
    case FieldRole::kPhone: {
      std::string digits;
      for (char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) digits += c;
        else if (c == '+' && digits.empty()) digits += c;
      }
      return digits.empty() || digits == "+" ? "" : "tel:" + digits;
    }
    case FieldRole::kEmail:
      return "mailto:" + text;
    case FieldRole::kUrl:
      return text.find("://") == std::string::npos ? "https://" + text : text;
    default:
      return "";
  }
}

}  // namespace fv
