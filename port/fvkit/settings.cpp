// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/settings.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fv {
namespace {

std::string Trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

std::string Lower(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

// Strips a trailing comment and unwraps a quoted value. A value may be quoted
// with " or ' to keep spaces and a literal # or ; — which paths do have.
std::string ValueOf(const std::string& raw) {
  const std::string v = Trim(raw);
  if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'')) {
    const size_t close = v.find(v.front(), 1);
    if (close != std::string::npos) return v.substr(1, close - 1);
  }
  const size_t cut = v.find_first_of("#;");
  return cut == std::string::npos ? v : Trim(v.substr(0, cut));
}

const char* GetEnv(const char* name) {
  const char* v = std::getenv(name);
  return (v != nullptr && *v != '\0') ? v : nullptr;
}

std::string Join(const std::string& dir, const std::string& tail) {
#ifdef _WIN32
  return dir + "\\" + tail;
#else
  return dir + "/" + tail;
#endif
}

bool Exists(const std::string& path) {
  std::ifstream f(path.c_str());
  return f.good();
}

}  // namespace

Status Settings::LoadFromString(const std::string& text,
                                const std::string& name) {
  std::map<std::string, std::string> parsed;
  std::istringstream in(text);
  std::string line, section;
  int lineno = 0;

  while (std::getline(in, line)) {
    ++lineno;
    const std::string t = Trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') continue;

    if (t[0] == '[') {
      const size_t close = t.find(']');
      if (close == std::string::npos)
        return Status::Error(kInvalidArg, name + ":" + std::to_string(lineno) +
                                              ": unterminated [section]");
      section = Lower(Trim(t.substr(1, close - 1)));
      if (section.empty())
        return Status::Error(
            kInvalidArg,
            name + ":" + std::to_string(lineno) + ": empty section name");
      continue;
    }

    const size_t eq = t.find('=');
    if (eq == std::string::npos)
      return Status::Error(kInvalidArg,
                           name + ":" + std::to_string(lineno) +
                               ": expected 'key = value', got: " + t);
    const std::string key = Lower(Trim(t.substr(0, eq)));
    if (key.empty())
      return Status::Error(kInvalidArg, name + ":" + std::to_string(lineno) +
                                            ": missing key before '='");

    // Last one wins, so a file can override an earlier line the way a user
    // expects when they paste a block at the bottom.
    parsed[section.empty() ? key : section + "." + key] =
        ValueOf(t.substr(eq + 1));
  }

  values_.swap(parsed);
  warnings_.clear();
  return Status::Ok();
}

Status Settings::Load(const std::string& path) {
  std::ifstream f(path.c_str());
  if (!f.good())
    return Status::Error(kNotFound, "no settings file at " + path);
  std::ostringstream buf;
  buf << f.rdbuf();
  Status s = LoadFromString(buf.str(), path);
  if (!s.ok()) return s;
  path_ = path;
  return Status::Ok();
}

Status Settings::LoadDefault() {
  for (const std::string& candidate : DefaultSettingsPaths()) {
    if (!Exists(candidate)) continue;
    return Load(candidate);
  }
  values_.clear();
  warnings_.clear();
  path_.clear();
  return Status::Ok();  // no file is normal
}

bool Settings::Has(const std::string& key) const {
  return values_.find(Lower(key)) != values_.end();
}

void Settings::Set(const std::string& key, const std::string& value) {
  values_[Lower(key)] = value;
}

std::vector<std::string> Settings::Keys() const {
  std::vector<std::string> out;
  out.reserve(values_.size());
  for (const auto& kv : values_) out.push_back(kv.first);
  return out;  // std::map is already sorted
}

void Settings::Warn(const std::string& key, const std::string& value,
                    const char* wanted, const std::string& used) const {
  warnings_.push_back(key + " = " + value + " is not " + wanted + ", using " +
                      used);
}

std::string Settings::GetString(const std::string& key,
                                const std::string& def) const {
  auto it = values_.find(Lower(key));
  return it == values_.end() ? def : it->second;
}

double Settings::GetDouble(const std::string& key, double def) const {
  auto it = values_.find(Lower(key));
  if (it == values_.end()) return def;
  const std::string& v = it->second;
  try {
    size_t used = 0;
    const double d = std::stod(v, &used);
    if (Trim(v.substr(used)).empty()) return d;
  } catch (const std::exception&) {
  }
  Warn(Lower(key), v, "a number", std::to_string(def));
  return def;
}

int Settings::GetInt(const std::string& key, int def) const {
  auto it = values_.find(Lower(key));
  if (it == values_.end()) return def;
  const std::string& v = it->second;
  try {
    size_t used = 0;
    const long n = std::stol(v, &used, 0);  // 0 = accept 0x.. and 0.. too
    if (Trim(v.substr(used)).empty() && n >= INT32_MIN && n <= INT32_MAX)
      return static_cast<int>(n);
  } catch (const std::exception&) {
  }
  Warn(Lower(key), v, "a whole number", std::to_string(def));
  return def;
}

bool Settings::GetBool(const std::string& key, bool def) const {
  auto it = values_.find(Lower(key));
  if (it == values_.end()) return def;
  const std::string v = Lower(it->second);
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  Warn(Lower(key), it->second, "a true/false value", def ? "true" : "false");
  return def;
}

std::vector<std::string> DefaultSettingsPaths() {
  std::vector<std::string> out;

  if (const char* explicit_path = GetEnv("FVW_SETTINGS"))
    out.push_back(explicit_path);

  out.push_back("peregrine.ini");  // the working directory

#ifdef _WIN32
  if (const char* appdata = GetEnv("APPDATA"))
    out.push_back(Join(Join(appdata, "Peregrine"), "settings.ini"));
#else
  if (const char* xdg = GetEnv("XDG_CONFIG_HOME")) {
    out.push_back(Join(Join(xdg, "peregrine"), "settings.ini"));
  } else if (const char* home = GetEnv("HOME")) {
    out.push_back(Join(Join(Join(home, ".config"), "peregrine"),
                       "settings.ini"));
  }
#ifdef __APPLE__
  if (const char* home = GetEnv("HOME")) {
    out.push_back(Join(Join(Join(home, "Library"), "Application Support"),
                       "Peregrine/settings.ini"));
  }
#endif
#endif
  return out;
}

}  // namespace fv
