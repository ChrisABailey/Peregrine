// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_route_doc.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fv {

const char kRouteFormat[] = "peregrine-route";
const char kRouteTypeId[] = "fv.route";
const char kRouteExtension[] = "fvrte";

namespace {

using json = nlohmann::json;

// --- Python's repr(float) ---------------------------------------------------
//
// CPython writes a float with `PyOS_double_to_string(v, 'r', 0, ...)`, which
// is: the shortest digit string that reads back as the same double, then a
// fixed/scientific choice made on `decpt` — the position of the decimal point
// relative to that digit string, so that value = 0.d1d2... x 10^decpt.
// Scientific when `decpt <= -4 || decpt > 16`; fixed otherwise, always with at
// least one digit after the point.
//
// The shortest digit string is found by the standard widening loop: `%.*e` at
// 1..17 significant digits, taking the first that survives a strtod round
// trip. 17 always round-trips (DBL_DIG + 2), so the loop terminates.

// Splits `v` into its shortest round-tripping digits and CPython's `decpt`.
// `v` must be finite and non-zero; sign is the caller's business.
void ShortestDigits(double v, std::string* digits, int* decpt) {
  char buf[64];
  for (int sig = 1; sig <= 17; ++sig) {
    std::snprintf(buf, sizeof(buf), "%.*e", sig - 1, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  // buf is "d.dddde[+-]XX" (or "de[+-]XX" at one significant digit).
  const char* e = std::strchr(buf, 'e');
  std::string mant(buf, e - buf);
  const int exp10 = std::atoi(e + 1);

  digits->clear();
  for (char c : mant) {
    if (c >= '0' && c <= '9') digits->push_back(c);
  }
  // A shortest representation never ENDS in a zero, but %.*e can still print
  // one when the round trip succeeded at that precision (1.0e+02 at three
  // significant digits). Strip them, so `decpt` is measured against the digits
  // CPython would have produced.
  while (digits->size() > 1 && digits->back() == '0') digits->pop_back();

  // value = 0.<digits> x 10^decpt, and mant = d.ddd x 10^exp10, so the point
  // sits one place further right than the exponent says.
  *decpt = exp10 + 1;
}

}  // namespace

std::string PythonFloatRepr(double v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v > 0 ? "Infinity" : "-Infinity";
  // 0.0 and -0.0: CPython prints "0.0" and "-0.0". The digit machinery has no
  // zero case (there is no exponent for it), so it is handled here.
  if (v == 0.0) return std::signbit(v) ? "-0.0" : "0.0";

  const bool neg = v < 0.0;
  std::string digits;
  int decpt = 0;
  ShortestDigits(neg ? -v : v, &digits, &decpt);

  std::string out;
  if (decpt <= -4 || decpt > 16) {
    // Scientific: d[.ddd]e[+-]XX, exponent at least two digits, which is what
    // both CPython and C's %e do.
    out += digits[0];
    if (digits.size() > 1) {
      out += '.';
      out.append(digits, 1, std::string::npos);
    }
    char tail[16];
    std::snprintf(tail, sizeof(tail), "e%+03d", decpt - 1);
    out += tail;
  } else if (decpt <= 0) {
    // 0.000ddd — `-decpt` zeros between the point and the first digit.
    out += "0.";
    out.append(static_cast<size_t>(-decpt), '0');
    out += digits;
  } else if (static_cast<size_t>(decpt) >= digits.size()) {
    // Integral: every digit left of the point, then padding zeros, then the
    // ".0" that makes it read back as a float rather than an int.
    out += digits;
    out.append(static_cast<size_t>(decpt) - digits.size(), '0');
    out += ".0";
  } else {
    out.append(digits, 0, static_cast<size_t>(decpt));
    out += '.';
    out.append(digits, static_cast<size_t>(decpt), std::string::npos);
  }
  return neg ? "-" + out : out;
}

namespace {

// `json.dumps`' string escaping with ensure_ascii=True (the default): the two
// mandatory escapes, the five short ones, \u00XX for the rest of C0, and
// \uXXXX for every code point above 0x7F — surrogate PAIRS above the BMP,
// which is where a naive escaper and Python part company.
void AppendJsonString(const std::string& s, std::string* out) {
  static const char* kHex = "0123456789abcdef";
  out->push_back('"');
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == '"' || c == '\\') {
      out->push_back('\\');
      out->push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    if (c == '\n') { *out += "\\n"; ++i; continue; }
    if (c == '\r') { *out += "\\r"; ++i; continue; }
    if (c == '\t') { *out += "\\t"; ++i; continue; }
    if (c == '\b') { *out += "\\b"; ++i; continue; }
    if (c == '\f') { *out += "\\f"; ++i; continue; }
    if (c < 0x20) {
      *out += "\\u00";
      out->push_back(kHex[(c >> 4) & 0xF]);
      out->push_back(kHex[c & 0xF]);
      ++i;
      continue;
    }
    if (c < 0x80) {
      out->push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    // Decode one UTF-8 sequence. Anything malformed is passed through byte for
    // byte: this writer's job is to reproduce a document, not to validate one,
    // and a name that arrived as bad UTF-8 should come back out unchanged
    // rather than as a replacement character.
    unsigned cp = 0;
    size_t len = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; len = 4; }
    bool ok = len != 0 && i + len <= s.size();
    for (size_t k = 1; ok && k < len; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) { ok = false; break; }
      cp = (cp << 6) | (cc & 0x3Fu);
    }
    if (!ok) {
      out->push_back(static_cast<char>(c));
      ++i;
      continue;
    }
    auto emit_unit = [&](unsigned u) {
      *out += "\\u";
      out->push_back(kHex[(u >> 12) & 0xF]);
      out->push_back(kHex[(u >> 8) & 0xF]);
      out->push_back(kHex[(u >> 4) & 0xF]);
      out->push_back(kHex[u & 0xF]);
    };
    if (cp >= 0x10000u) {
      const unsigned v = cp - 0x10000u;
      emit_unit(0xD800u + (v >> 10));
      emit_unit(0xDC00u + (v & 0x3FFu));
    } else {
      emit_unit(cp);
    }
    i += len;
  }
  out->push_back('"');
}

// Reads a double out of a JSON value that may legitimately be an int
// (`"lat": 32` is a valid document, and route.py's own reader calls float()).
bool JsonNumber(const json& v, double* out) {
  if (!v.is_number()) return false;
  *out = v.get<double>();
  return true;
}

}  // namespace

void RouteDoc::Reset() {
  name_.clear();
  color_ = FvColor{220, 30, 30, 255};
  profile_.clear();
  waypoints_.clear();
}

std::string RouteDoc::ToJson() const {
  // Key order is route.py's dict INSERTION order, which is what json.dump
  // preserves: format, version, name, color, profile, waypoints.
  std::string s;
  s += "{\n";
  s += "  \"format\": ";
  AppendJsonString(kRouteFormat, &s);
  s += ",\n";
  s += "  \"version\": " + std::to_string(kRouteVersion) + ",\n";
  s += "  \"name\": ";
  AppendJsonString(name_, &s);
  s += ",\n";
  // A list of scalars still goes one per line under indent=2 — json.dump never
  // puts a non-empty container on one line — which is why the fixture's colour
  // is five lines of file.
  s += "  \"color\": [\n";
  s += "    " + std::to_string(static_cast<int>(color_.r)) + ",\n";
  s += "    " + std::to_string(static_cast<int>(color_.g)) + ",\n";
  s += "    " + std::to_string(static_cast<int>(color_.b)) + "\n";
  s += "  ],\n";
  s += "  \"profile\": ";
  AppendJsonString(profile_, &s);
  s += ",\n";
  if (waypoints_.empty()) {
    // json.dump writes an EMPTY container on one line: there is nothing to
    // indent, so `[]` it is. Getting this wrong is invisible until a shell
    // saves a route with no waypoints in it.
    s += "  \"waypoints\": []\n";
  } else {
    s += "  \"waypoints\": [\n";
    for (size_t i = 0; i < waypoints_.size(); ++i) {
      const RouteWaypoint& w = waypoints_[i];
      s += "    {\n";
      s += "      \"label\": ";
      AppendJsonString(w.label, &s);
      s += ",\n";
      s += "      \"lat\": " + PythonFloatRepr(w.position.lat) + ",\n";
      s += "      \"lon\": " + PythonFloatRepr(w.position.lon) + "\n";
      s += "    }";
      s += (i + 1 == waypoints_.size()) ? "\n" : ",\n";
    }
    s += "  ]\n";
  }
  s += "}\n";  // route.py's explicit f.write("\n") after json.dump
  return s;
}

Status RouteDoc::Parse(const std::string& text, const std::string& origin) {
  json doc;
  try {
    doc = json::parse(text);
  } catch (const std::exception& e) {
    return Status::Error(kIoError, origin + ": " + e.what());
  }
  if (!doc.is_object()) {
    return Status::Error(kIoError, origin + " is not a route document");
  }
  const auto fmt = doc.find("format");
  if (fmt == doc.end() || !fmt->is_string() ||
      fmt->get<std::string>() != kRouteFormat) {
    return Status::Error(kIoError, origin + " is not a route document");
  }
  const auto ver = doc.find("version");
  int version = 0;
  if (ver != doc.end() && ver->is_number()) version = ver->get<int>();
  if (version > kRouteVersion) {
    return Status::Error(
        kUnsupported, origin + ": route version " + std::to_string(version) +
                          " is newer than this build understands (" +
                          std::to_string(kRouteVersion) + ")");
  }

  // Everything below builds into LOCALS: a document that fails half way
  // through must not leave the caller with half a route (the "untouched on
  // failure" promise in the header).
  std::vector<RouteWaypoint> waypoints;
  const auto wps = doc.find("waypoints");
  if (wps != doc.end() && wps->is_array()) {
    waypoints.reserve(wps->size());
    for (const auto& w : *wps) {
      if (!w.is_object()) {
        return Status::Error(kIoError, origin + ": a waypoint is not an object");
      }
      RouteWaypoint out;
      const auto label = w.find("label");
      if (label != w.end() && label->is_string()) {
        out.label = label->get<std::string>();
      }
      const auto lat = w.find("lat");
      const auto lon = w.find("lon");
      if (lat == w.end() || lon == w.end() ||
          !JsonNumber(*lat, &out.position.lat) ||
          !JsonNumber(*lon, &out.position.lon)) {
        return Status::Error(kIoError, origin + ": waypoint \"" + out.label +
                                           "\" has no usable lat/lon");
      }
      waypoints.push_back(std::move(out));
    }
  }

  // The optional fields follow route.py's reader exactly, INCLUDING its
  // falsiness: an empty name or an empty colour list leaves the current value
  // alone, so `"name": ""` does not blank an overlay's name.
  FvColor color = color_;
  const auto col = doc.find("color");
  if (col != doc.end() && col->is_array() && !col->empty()) {
    unsigned char* ch[3] = {&color.r, &color.g, &color.b};
    for (size_t i = 0; i < 3 && i < col->size(); ++i) {
      double v = 0.0;
      if (JsonNumber((*col)[i], &v)) {
        if (v < 0.0) v = 0.0;
        if (v > 255.0) v = 255.0;
        *ch[i] = static_cast<unsigned char>(v);
      }
    }
    color.a = 255;
  }
  std::string name = name_;
  const auto nm = doc.find("name");
  if (nm != doc.end() && nm->is_string() && !nm->get<std::string>().empty()) {
    name = nm->get<std::string>();
  }
  std::string profile = profile_;
  const auto pf = doc.find("profile");
  if (pf != doc.end() && pf->is_string()) profile = pf->get<std::string>();

  waypoints_ = std::move(waypoints);
  color_ = color;
  name_ = std::move(name);
  profile_ = std::move(profile);
  return Status::Ok();
}

Status RouteDoc::Read(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kNotFound, "cannot open route file: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return Parse(ss.str(), path);
}

Status RouteDoc::Write(const std::string& path) const {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return Status::Error(kIoError, "cannot write route file: " + path);
  const std::string text = ToJson();
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!out) return Status::Error(kIoError, "short write: " + path);
  return Status::Ok();
}

}  // namespace fv
