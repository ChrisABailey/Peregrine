// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_user_settings.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "fvkit/settings.h"

namespace fv {
namespace desk {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr int kFormatVersion = 1;

// fv::Settings keys are case-insensitive and stored lower-case; these match.
std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

const char* Env(const char* name) {
  const char* v = std::getenv(name);
  return (v != nullptr && *v != '\0') ? v : nullptr;
}

}  // namespace

Status UserSettings::Load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return Status::Ok();
    return Status::Error(kIoError, "cannot read " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  json doc = json::parse(ss.str(), nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object())
    return Status::Error(kInvalidArg, path + ": not a JSON object");
  if (doc.value("version", 0) > kFormatVersion)
    return Status::Error(kUnsupported, path + ": written by a newer version");
  std::map<std::string, std::string> values;
  if (doc.contains("values")) {
    if (!doc["values"].is_object())
      return Status::Error(kInvalidArg, path + ": \"values\" is not an object");
    for (auto it = doc["values"].begin(); it != doc["values"].end(); ++it) {
      if (!it.value().is_string())
        return Status::Error(kInvalidArg, path + ": value of '" + it.key() + "' is not a string");
      values[Lower(it.key())] = it.value().get<std::string>();
    }
  }
  values_ = std::move(values);
  dirty_ = false;
  ++generation_;
  return Status::Ok();
}

Status UserSettings::Save(const std::string& path) const {
  if (path.empty()) return Status::Error(kInvalidArg, "no user settings path");
  std::error_code ec;
  const fs::path target(path);
  if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
  if (ec) return Status::Error(kIoError, "cannot create " + target.parent_path().string());

  json doc;
  doc["version"] = kFormatVersion;
  doc["values"] = json::object();
  for (const auto& kv : values_) doc["values"][kv.first] = kv.second;

  const std::string tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return Status::Error(kIoError, "cannot write " + tmp);
    out << doc.dump(2) << '\n';
    if (!out) return Status::Error(kIoError, "cannot write " + tmp);
  }
  fs::rename(tmp, target, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return Status::Error(kIoError, "cannot replace " + path);
  }
  dirty_ = false;
  return Status::Ok();
}

std::string UserSettings::Get(const std::string& key, const std::string& def) const {
  auto it = values_.find(Lower(key));
  return it == values_.end() ? def : it->second;
}

void UserSettings::Set(const std::string& key, const std::string& value) {
  auto ins = values_.emplace(Lower(key), value);
  if (!ins.second) {
    if (ins.first->second == value) return;
    ins.first->second = value;
  }
  dirty_ = true;
  ++generation_;
}

bool UserSettings::Erase(const std::string& key) {
  if (values_.erase(Lower(key)) == 0) return false;
  dirty_ = true;
  ++generation_;
  return true;
}

size_t UserSettings::ErasePrefix(const std::string& prefix) {
  const std::string p = Lower(prefix);
  size_t n = 0;
  for (auto it = values_.lower_bound(p);
       it != values_.end() && it->first.compare(0, p.size(), p) == 0;) {
    it = values_.erase(it);
    ++n;
  }
  if (n > 0) {
    dirty_ = true;
    ++generation_;
  }
  return n;
}

std::vector<std::string> UserSettings::Keys() const {
  std::vector<std::string> out;
  out.reserve(values_.size());
  for (const auto& kv : values_) out.push_back(kv.first);
  return out;
}

void UserSettings::ApplyTo(Settings& settings) const {
  for (const auto& kv : values_) settings.Set(kv.first, kv.second);
}

void UserSettings::CaptureFrom(const Settings& settings, const std::string& prefix) {
  const std::string p = Lower(prefix);
  for (const std::string& key : settings.Keys())
    if (key.compare(0, p.size(), p) == 0) Set(key, settings.GetString(key));
}

std::string DefaultUserSettingsPath() {
  const char* file = "user-settings.json";
#if defined(_WIN32)
  if (const char* appdata = Env("APPDATA"))
    return (fs::path(appdata) / "Peregrine" / file).string();
  return std::string();
#elif defined(__APPLE__)
  if (const char* home = Env("HOME"))
    return (fs::path(home) / "Library" / "Application Support" / "Peregrine" / file).string();
  return std::string();
#else
  if (const char* xdg = Env("XDG_CONFIG_HOME"))
    return (fs::path(xdg) / "peregrine" / file).string();
  if (const char* home = Env("HOME"))
    return (fs::path(home) / ".config" / "peregrine" / file).string();
  return std::string();
#endif
}

}  // namespace desk
}  // namespace fv
