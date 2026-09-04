// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

// PropertyValue / Properties helpers — see fvkit/app/properties.h.

#include "fvkit/app/properties.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "fvkit/settings.h"

namespace fv {
namespace app {

namespace {

bool ParseHexByte(const char* p, int* out) {
  int v = 0;
  for (int i = 0; i < 2; ++i) {
    const char c = p[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = v * 16 + d;
  }
  *out = v;
  return true;
}

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  auto space = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (b < e && space(s[b])) ++b;
  while (e > b && space(s[e - 1])) --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
  return s;
}


// Reads a colour in any of the spellings somebody actually writes.
//
// LENIENT IN, CANONICAL OUT: ToString always emits #RRGGBBAA, so a settings
// file this rewrote would be normalised -- but nothing rewrites the user's
// file (peregrine.ini.sample's rule S1), so what they typed is what stays
// there. The accepted forms:
//
//   #RGB  #RGBA          shorthand, each digit doubled (CSS)
//   #RRGGBB  #RRGGBBAA   a missing alpha is opaque, which is what a human
//                        writing a colour by hand means
//   rgb(r, g, b)         components 0-255
//   rgba(r, g, b, a)     ALPHA IS A CSS FRACTION, 0.0-1.0
//
// The rgb()/rgba() forms are here because they are what a colour picker hands
// you and what Chris reached for the first time he styled the graticule
// (2026-08-29); the hex-only parser rejected `rgba(32, 37, 31, 0.97)` and the
// label silently kept its default. A value that will not parse must be a
// warning somebody SEES -- that is the other half of that bug -- but the first
// half is not rejecting a reasonable thing in the first place.
//
// A trailing alpha of exactly 1 is CSS's "opaque", never 1/255. That is the
// one place the two conventions genuinely disagree and CSS wins, because
// rgba() IS the CSS spelling; write hex if you want to count in bytes.
bool ParseColor(const std::string& in, FvColor* out) {
  std::string t = Trim(in);
  if (t.empty()) return false;

  if (t[0] == '#') {
    const std::string h = t.substr(1);
    int v[4] = {0, 0, 0, 255};
    if (h.size() == 3 || h.size() == 4) {
      for (size_t i = 0; i < h.size(); ++i) {
        const char d[3] = {h[i], h[i], '\0'};
        if (!ParseHexByte(d, &v[i])) return false;
      }
    } else if (h.size() == 6 || h.size() == 8) {
      for (size_t i = 0; i * 2 < h.size(); ++i)
        if (!ParseHexByte(h.c_str() + i * 2, &v[i])) return false;
    } else {
      return false;
    }
    out->r = static_cast<unsigned char>(v[0]);
    out->g = static_cast<unsigned char>(v[1]);
    out->b = static_cast<unsigned char>(v[2]);
    out->a = static_cast<unsigned char>(v[3]);
    return true;
  }

  const std::string lower = Lower(t);
  const bool has_alpha = lower.compare(0, 5, "rgba(") == 0;
  if (!has_alpha && lower.compare(0, 4, "rgb(") != 0) return false;
  if (t.back() != ')') return false;

  const size_t open = t.find('(');
  std::string body = t.substr(open + 1, t.size() - open - 2);
  double c[4] = {0.0, 0.0, 0.0, 1.0};
  int n = 0;
  size_t pos = 0;
  while (pos <= body.size() && n < 5) {
    size_t comma = body.find(',', pos);
    const std::string field =
        Trim(body.substr(pos, comma == std::string::npos ? std::string::npos
                                                         : comma - pos));
    if (field.empty()) return false;
    char* end = nullptr;
    const double val = std::strtod(field.c_str(), &end);
    if (end == field.c_str() || *end != '\0') return false;
    if (n < 4) c[n] = val;
    ++n;
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  if (n != (has_alpha ? 4 : 3)) return false;

  auto byte = [](double v) {
    if (v < 0.0) v = 0.0;
    if (v > 255.0) v = 255.0;
    return static_cast<unsigned char>(v + 0.5);
  };
  out->r = byte(c[0]);
  out->g = byte(c[1]);
  out->b = byte(c[2]);
  if (c[3] < 0.0) c[3] = 0.0;
  if (c[3] > 1.0) c[3] = 1.0;
  out->a = static_cast<unsigned char>(c[3] * 255.0 + 0.5);
  return true;
}

bool SameValue(const PropertyValue& a, const PropertyValue& b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case PropertyType::kBool: return a.b == b.b;
    case PropertyType::kInt:
    case PropertyType::kChoice: return a.i == b.i;
    case PropertyType::kDouble: return a.d == b.d;
    case PropertyType::kString: return a.s == b.s;
    case PropertyType::kColor:
      return a.color.r == b.color.r && a.color.g == b.color.g &&
             a.color.b == b.color.b && a.color.a == b.color.a;
  }
  return false;
}

}  // namespace

// --- PropertyValue ---------------------------------------------------------

PropertyValue PropertyValue::Bool(bool v) {
  PropertyValue p;
  p.type = PropertyType::kBool;
  p.b = v;
  return p;
}
PropertyValue PropertyValue::Int(long long v) {
  PropertyValue p;
  p.type = PropertyType::kInt;
  p.i = v;
  return p;
}
PropertyValue PropertyValue::Double(double v) {
  PropertyValue p;
  p.type = PropertyType::kDouble;
  p.d = v;
  return p;
}
PropertyValue PropertyValue::String(std::string v) {
  PropertyValue p;
  p.type = PropertyType::kString;
  p.s = std::move(v);
  return p;
}
PropertyValue PropertyValue::Color(FvColor v) {
  PropertyValue p;
  p.type = PropertyType::kColor;
  p.color = v;
  return p;
}
PropertyValue PropertyValue::Choice(long long index) {
  PropertyValue p;
  p.type = PropertyType::kChoice;
  p.i = index;
  return p;
}

std::string PropertyValue::ToString() const {
  char buf[64];
  switch (type) {
    case PropertyType::kBool:
      return b ? "true" : "false";
    case PropertyType::kInt:
    case PropertyType::kChoice:
      std::snprintf(buf, sizeof(buf), "%lld", i);
      return buf;
    case PropertyType::kDouble:
      // %.10g: round-trips every value a property page can produce without
      // printing 17 digits of noise into a file a human reads.
      std::snprintf(buf, sizeof(buf), "%.10g", d);
      return buf;
    case PropertyType::kString:
      return s;
    case PropertyType::kColor:
      std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", color.r, color.g,
                    color.b, color.a);
      return buf;
  }
  return std::string();
}

bool PropertyValue::FromString(const std::string& text) {
  switch (type) {
    case PropertyType::kBool: {
      const std::string t = Lower(text);
      if (t == "true" || t == "yes" || t == "on" || t == "1") {
        b = true;
        return true;
      }
      if (t == "false" || t == "no" || t == "off" || t == "0") {
        b = false;
        return true;
      }
      return false;
    }
    case PropertyType::kInt:
    case PropertyType::kChoice: {
      char* end = nullptr;
      const long long v = std::strtoll(text.c_str(), &end, 10);
      if (end == text.c_str() || *end != '\0') return false;
      i = v;
      return true;
    }
    case PropertyType::kDouble: {
      char* end = nullptr;
      const double v = std::strtod(text.c_str(), &end);
      if (end == text.c_str() || *end != '\0') return false;
      d = v;
      return true;
    }
    case PropertyType::kString:
      s = text;
      return true;
    case PropertyType::kColor:
      return ParseColor(text, &color);
  }
  return false;
}

std::string SettingsPrefixForTypeId(const std::string& type_id) {
  if (type_id.empty()) return std::string();
  const size_t dot = type_id.rfind('.');
  const std::string leaf =
      dot == std::string::npos ? type_id : type_id.substr(dot + 1);
  if (leaf.empty()) return std::string();
  return leaf + ".";
}

// --- Properties helpers ----------------------------------------------------

const PropertySpec* Properties::FindSpec(const std::string& key) const {
  for (const PropertySpec& s : Describe())
    if (s.key == key) return &s;
  return nullptr;
}

// A kChoice in a settings file is written by NAME -- `smoothing = chaikin`,
// not `smoothing = 1`. The index is what the value IS, but a settings file is
// read and diffed by people, which is the same argument R4 made for a string
// TypeId. A number still parses, so nothing that wrote one is broken; the
// contour overlay is the first type to declare a choice at all, so there is
// no older spelling to keep working.
namespace {

bool ChoiceIndexForName(const PropertySpec& spec, const std::string& text,
                        long long* out) {
  const std::string want = Lower(text);
  for (size_t i = 0; i < spec.choices.size(); ++i) {
    if (Lower(spec.choices[i]) == want) {
      *out = static_cast<long long>(i);
      return true;
    }
  }
  return false;
}

}  // namespace

Status Properties::LoadFrom(const Settings& settings, const std::string& prefix,
                            std::vector<std::string>* warnings) {
  for (const PropertySpec& spec : Describe()) {
    const std::string full = prefix + spec.key;
    if (!settings.Has(full)) continue;
    PropertyValue v = spec.default_value;
    v.type = spec.type;
    const std::string raw = settings.GetString(full);
    if (spec.type == PropertyType::kChoice &&
        ChoiceIndexForName(spec, raw, &v.i)) {
      Status s = SetProperty(spec.key, v);
      if (!s.ok() && warnings != nullptr)
        warnings->push_back(full + " = " + raw + " rejected: " + s.message);
      continue;
    }
    if (!v.FromString(raw)) {
      if (warnings != nullptr)
        warnings->push_back(full + " = " + raw +
                            " is not a valid value, keeping current");
      continue;
    }
    Status s = SetProperty(spec.key, v);
    if (!s.ok() && warnings != nullptr)
      warnings->push_back(full + " = " + raw + " rejected: " + s.message);
  }
  return Status::Ok();
}

Status Properties::SaveTo(Settings* settings, const std::string& prefix,
                          bool only_changed) const {
  if (settings == nullptr) return Status::Error(kInvalidArg, "null settings");
  for (const PropertySpec& spec : Describe()) {
    PropertyValue v;
    Status s = GetProperty(spec.key, &v);
    if (!s.ok()) return s;
    if (only_changed && SameValue(v, spec.default_value)) continue;
    // Written back the way LoadFrom reads it: a choice by name.
    if (spec.type == PropertyType::kChoice && v.i >= 0 &&
        v.i < static_cast<long long>(spec.choices.size()))
      settings->Set(prefix + spec.key,
                    spec.choices[static_cast<size_t>(v.i)]);
    else
      settings->Set(prefix + spec.key, v.ToString());
  }
  return Status::Ok();
}

Status Properties::ResetToDefaults() {
  // Describe() returns a reference the overlay owns; SetProperty may not
  // invalidate it (the header says the schema is stable), so iterating it
  // directly is safe and is what every implementation does.
  for (const PropertySpec& spec : Describe()) {
    Status s = SetProperty(spec.key, spec.default_value);
    if (!s.ok()) return s;
  }
  return Status::Ok();
}

bool Properties::GetBool(const std::string& key, bool def) const {
  PropertyValue v;
  if (!GetProperty(key, &v).ok() || v.type != PropertyType::kBool) return def;
  return v.b;
}
long long Properties::GetInt(const std::string& key, long long def) const {
  PropertyValue v;
  if (!GetProperty(key, &v).ok()) return def;
  if (v.type != PropertyType::kInt && v.type != PropertyType::kChoice)
    return def;
  return v.i;
}
double Properties::GetDouble(const std::string& key, double def) const {
  PropertyValue v;
  if (!GetProperty(key, &v).ok() || v.type != PropertyType::kDouble) return def;
  return v.d;
}
std::string Properties::GetString(const std::string& key,
                                  const std::string& def) const {
  PropertyValue v;
  if (!GetProperty(key, &v).ok() || v.type != PropertyType::kString) return def;
  return v.s;
}
FvColor Properties::GetColor(const std::string& key, FvColor def) const {
  PropertyValue v;
  if (!GetProperty(key, &v).ok() || v.type != PropertyType::kColor) return def;
  return v.color;
}

}  // namespace app
}  // namespace fv
