// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_overlay_manifest.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "fv_desk_user_settings.h"
#include "fvkit/log.h"

namespace fv {
namespace desk {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct BuiltinTable {
  std::mutex mutex;
  std::map<std::string, BuiltinOverlay> entries;
};

BuiltinTable& Builtins() {
  static BuiltinTable table;
  return table;
}

bool IsIconFile(const std::string& icon) {
  std::string ext = fs::path(icon).extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".svg" || ext == ".png" || ext == ".pdf";
}

/// Reads `[["label", "*.ext"], ...]` into filter pairs.
bool ReadFilters(const json& j, std::vector<std::pair<std::string, std::string>>* out) {
  if (!j.is_array()) return false;
  std::vector<std::pair<std::string, std::string>> v;
  for (const json& f : j) {
    if (!f.is_array() || f.size() != 2 || !f[0].is_string() || !f[1].is_string()) return false;
    v.emplace_back(f[0].get<std::string>(), f[1].get<std::string>());
  }
  *out = std::move(v);
  return true;
}

/// Parses one entry object; `why` names the first problem.
bool ParseEntry(const json& j, const std::string& base_dir, OverlayManifestEntry* e,
                std::string* why) {
  if (!j.is_object()) return *why = "every entry must be an object", false;
  if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty())
    return *why = "an entry has no id", false;
  app::OverlayTypeDesc& d = e->desc;
  d.id = j["id"].get<std::string>();
  const std::string at = "'" + d.id + "': ";

  auto str = [&](const char* key, std::string* dst) {
    if (!j.contains(key)) return true;
    if (!j[key].is_string()) return *why = at + key + " must be a string", false;
    *dst = j[key].get<std::string>();
    return true;
  };
  auto flag = [&](const char* key, bool* dst) {
    if (!j.contains(key)) return true;
    if (!j[key].is_boolean()) return *why = at + key + " must be true or false", false;
    *dst = j[key].get<bool>();
    return true;
  };
  auto integer = [&](const char* key, int* dst) {
    if (!j.contains(key)) return true;
    if (!j[key].is_number_integer()) return *why = at + key + " must be an integer", false;
    *dst = j[key].get<int>();
    return true;
  };
  if (!str("display_name", &d.display_name) ||
      !str("parent_display_name", &d.parent_display_name) || !str("icon", &d.icon) ||
      !integer("display_order", &d.default_display_order) ||
      !integer("opacity", &d.default_opacity) || !flag("top_most", &d.is_top_most) ||
      !flag("user_controllable", &d.user_controllable) ||
      !flag("restore_at_startup", &d.restore_at_startup))
    return false;
  if (d.default_opacity < 0 || d.default_opacity > 100)
    return *why = at + "opacity must be 0 to 100", false;
  if (!d.icon.empty() && IsIconFile(d.icon) && !base_dir.empty() &&
      fs::path(d.icon).is_relative())
    d.icon = (fs::path(base_dir) / d.icon).lexically_normal().string();

  std::string kind;
  if (!str("kind", &kind)) return false;
  const bool has_file = j.contains("file");
  if (kind.empty()) kind = has_file ? "file" : "static";
  if (kind != "static" && kind != "file")
    return *why = at + "kind must be \"static\" or \"file\"", false;
  if (kind == "static" && has_file) return *why = at + "a static type has no \"file\"", false;
  if (kind == "file") {
    if (!has_file || !j["file"].is_object())
      return *why = at + "a file type needs a \"file\" object", false;
    const json& jf = j["file"];
    app::FileTypeDesc f;
    if (!jf.contains("extension") || !jf["extension"].is_string())
      return *why = at + "file.extension is required", false;
    f.default_extension = jf["extension"].get<std::string>();
    if (!f.default_extension.empty() && f.default_extension[0] == '.')
      f.default_extension.erase(0, 1);
    if (f.default_extension.empty()) return *why = at + "file.extension is empty", false;
    if (jf.contains("directory")) {
      if (!jf["directory"].is_string()) return *why = at + "file.directory must be a string", false;
      f.default_directory = jf["directory"].get<std::string>();
    }
    if (jf.contains("filters") && !ReadFilters(jf["filters"], &f.open_filters))
      return *why = at + "file.filters must be [[label, pattern], ...]", false;
    if (jf.contains("save_filters")) {
      if (!ReadFilters(jf["save_filters"], &f.save_filters))
        return *why = at + "file.save_filters must be [[label, pattern], ...]", false;
    } else {
      f.save_filters = f.open_filters;
    }
    if (f.open_filters.empty()) {
      const std::string pattern = "*." + f.default_extension;
      const std::string label = d.display_name.empty() ? d.id : d.display_name;
      f.open_filters = {{label + " (" + pattern + ")", pattern}};
      if (f.save_filters.empty()) f.save_filters = f.open_filters;
    }
    d.file = std::move(f);
  }

  if (!j.contains("implementation") || !j["implementation"].is_object() ||
      j["implementation"].size() != 1)
    return *why = at + "implementation must be an object with one key", false;
  const auto impl = j["implementation"].begin();
  if (impl.key() != "builtin" && impl.key() != "library" && impl.key() != "python")
    return *why = at + "unknown implementation kind '" + impl.key() + "'", false;
  if (!impl.value().is_string() || impl.value().get<std::string>().empty())
    return *why = at + "implementation." + impl.key() + " must name something", false;
  e->implementation_kind = impl.key();
  e->implementation_name = impl.value().get<std::string>();
  return true;
}

}  // namespace

// MARK: Builtin table

Status RegisterBuiltinOverlay(const std::string& name, BuiltinOverlay impl) {
  if (name.empty()) return Status::Error(kInvalidArg, "builtin overlay with no name");
  if (!impl.factory) return Status::Error(kInvalidArg, "builtin overlay '" + name + "' has no factory");
  BuiltinTable& t = Builtins();
  std::lock_guard<std::mutex> lock(t.mutex);
  if (!t.entries.emplace(name, std::move(impl)).second)
    return Status::Error(kInvalidArg, "builtin overlay '" + name + "' is already registered");
  return Status::Ok();
}

const BuiltinOverlay* FindBuiltinOverlay(const std::string& name) {
  BuiltinTable& t = Builtins();
  std::lock_guard<std::mutex> lock(t.mutex);
  // std::map nodes are stable, so the pointer outlives the lock.
  auto it = t.entries.find(name);
  return it == t.entries.end() ? nullptr : &it->second;
}

bool UnregisterBuiltinOverlay(const std::string& name) {
  BuiltinTable& t = Builtins();
  std::lock_guard<std::mutex> lock(t.mutex);
  return t.entries.erase(name) != 0;
}

BuiltinOverlayRegistrar::BuiltinOverlayRegistrar(const std::string& name, BuiltinOverlay impl) {
  RegisterBuiltinOverlay(name, std::move(impl));
}

// MARK: Parsing

Status ParseOverlayManifest(const std::string& text, const std::string& source,
                            const std::string& base_dir,
                            std::vector<OverlayManifestEntry>* out) {
  auto fail = [&source](const std::string& why) {
    return Status::Error(kInvalidArg, source + ": " + why);
  };
  const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded()) return fail("not valid JSON");
  std::vector<const json*> objects;
  if (doc.is_object() && doc.contains("overlays")) {
    if (!doc["overlays"].is_array()) return fail("\"overlays\" must be an array");
    for (const json& j : doc["overlays"]) objects.push_back(&j);
  } else if (doc.is_object()) {
    objects.push_back(&doc);
  } else {
    return fail("expected an entry object or {\"overlays\": [...]}");
  }

  std::vector<OverlayManifestEntry> parsed;
  for (const json* j : objects) {
    OverlayManifestEntry e;
    std::string why;
    if (!ParseEntry(*j, base_dir, &e, &why)) return fail(why);
    for (const OverlayManifestEntry& seen : parsed)
      if (seen.desc.id == e.desc.id) return fail("'" + e.desc.id + "' appears twice");
    e.source = source;
    parsed.push_back(std::move(e));
  }
  out->insert(out->end(), std::make_move_iterator(parsed.begin()),
              std::make_move_iterator(parsed.end()));
  return Status::Ok();
}

Status ReadOverlayManifestFile(const std::string& path, std::vector<OverlayManifestEntry>* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kNotFound, "cannot read " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ParseOverlayManifest(ss.str(), path, fs::path(path).parent_path().string(), out);
}

// MARK: Registration

Status RegisterManifestEntry(app::OverlayTypeRegistry& registry, const OverlayManifestEntry& entry) {
  const std::string& id = entry.desc.id;
  if (entry.implementation_kind != "builtin")
    return Status::Error(kUnsupported, "'" + id + "': " + entry.implementation_kind +
                                           " overlays are not supported yet");
  const BuiltinOverlay* impl = FindBuiltinOverlay(entry.implementation_name);
  if (impl == nullptr)
    return Status::Error(kNotFound, "'" + id + "': no builtin overlay named '" +
                                        entry.implementation_name + "'");
  app::OverlayTypeDesc d = entry.desc;
  d.factory = impl->factory;
  d.editor_factory = impl->editor_factory;
  return registry.Register(std::move(d));
}

std::vector<app::TypeId> RegisterOverlayManifests(app::OverlayTypeRegistry& registry,
                                                  const std::vector<std::string>& dirs,
                                                  std::vector<std::string>* warnings) {
  auto warn = [warnings](const std::string& w) {
    FV_LOG_WARNING("overlay manifest: " << w);
    if (warnings != nullptr) warnings->push_back(w);
  };
  std::vector<app::TypeId> registered;
  for (const std::string& dir : dirs) {
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) continue;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
      if (it->path().extension() == ".json" && it->is_regular_file(ec)) files.push_back(it->path());
    if (ec) warn("overlay manifests in " + dir + ": " + ec.message());
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
      std::vector<OverlayManifestEntry> entries;
      const Status s = ReadOverlayManifestFile(f.string(), &entries);
      if (!s.ok()) {
        warn(s.message);
        continue;
      }
      for (const OverlayManifestEntry& e : entries) {
        const Status r = RegisterManifestEntry(registry, e);
        if (r.ok()) {
          registered.push_back(e.desc.id);
        } else {
          warn(e.source + ": " + r.message);
        }
      }
    }
  }
  return registered;
}

std::string DefaultUserOverlayManifestDir() {
  const std::string settings = DefaultUserSettingsPath();
  if (settings.empty()) return std::string();
  return (fs::path(settings).parent_path() / "overlays").string();
}

}  // namespace desk
}  // namespace fv
