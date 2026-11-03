// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_packages.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "fv_desk_library_plugin.h"
#include "fv_desk_native_plugin.h"
#include "fv_desk_python_plugin.h"
#include "fv_desk_user_settings.h"
#include "fv_zip_reader.h"
#include "fvkit/log.h"

namespace fv {
namespace desk {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char* kPlatformKeys[] = {"macos",         "macos-arm64",   "macos-x86_64",
                                         "linux-x86_64",  "linux-aarch64", "windows-x86_64"};
constexpr char kStateFile[] = ".state.json";
constexpr char kPendingDir[] = ".pending";
constexpr char kStagingDir[] = ".staging";

/// Absolute, lexically normal, no trailing separator.
std::string Normal(const fs::path& p) {
  std::error_code ec;
  fs::path a = fs::absolute(p, ec).lexically_normal();
  if (a.filename().empty() && a.has_parent_path()) a = a.parent_path();
  return a.string();
}

/// Relative, forward slashes only, no "..", no empty components.
bool IsSafeRelativePath(const std::string& s) {
  return !s.empty() && s.find('\\') == std::string::npos && s.find(':') == std::string::npos &&
         IsSafeZipPath(s);
}

bool IsIconFile(const std::string& icon) {
  std::string ext = fs::path(icon).extension().string();
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext == ".svg" || ext == ".png" || ext == ".pdf";
}

/// Reverse-DNS: two or more dot-separated segments of letters, digits, '-'
/// and '_', each starting with a letter or digit.
bool IsValidPackageId(const std::string& id) {
  size_t segments = 0;
  size_t start = 0;
  while (start <= id.size()) {
    const size_t dot = std::min(id.find('.', start), id.size());
    const std::string seg = id.substr(start, dot - start);
    if (seg.empty() || !std::isalnum(static_cast<unsigned char>(seg[0]))) return false;
    for (char c : seg)
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    ++segments;
    start = dot + 1;
  }
  return segments >= 2;
}

bool IsReservedId(const std::string& id) {
  return id.rfind("fv.", 0) == 0 || id.rfind("peregrine.", 0) == 0;
}

bool IsVersion(const std::string& v) {
  if (v.empty() || v.front() == '.' || v.back() == '.') return false;
  for (size_t i = 0; i < v.size(); ++i) {
    if (v[i] == '.' ? v[i - 1] == '.' : !std::isdigit(static_cast<unsigned char>(v[i])))
      return false;
  }
  return true;
}

bool HasPrefixId(const std::string& id, const std::string& package_id) {
  return id.size() > package_id.size() + 1 && id.compare(0, package_id.size(), package_id) == 0 &&
         id[package_id.size()] == '.';
}

/// Reads `{"<kind>": "<name>"}`; `why` names the problem.
bool ReadImplementation(const json& j, const std::string& at, std::string* kind, std::string* name,
                        std::string* why) {
  if (!j.contains("implementation") || !j["implementation"].is_object() ||
      j["implementation"].size() != 1)
    return *why = at + "implementation must be an object with one key", false;
  const auto impl = j["implementation"].begin();
  if (impl.key() != "builtin" && impl.key() != "library" && impl.key() != "native" &&
      impl.key() != "python")
    return *why = at + "unknown implementation kind '" + impl.key() + "'", false;
  if (!impl.value().is_string() || impl.value().get<std::string>().empty())
    return *why = at + "implementation." + impl.key() + " must name something", false;
  *kind = impl.key();
  *name = impl.value().get<std::string>();
  return true;
}

bool ParseTool(const json& j, const std::string& package_id, const std::string& root,
               ToolManifestEntry* e, std::string* why) {
  if (!j.is_object()) return *why = "every tool must be an object", false;
  if (!j.contains("id") || !j["id"].is_string()) return *why = "a tool has no id", false;
  ToolDesc& d = e->desc;
  d.id = j["id"].get<std::string>();
  const std::string at = "tool '" + d.id + "': ";
  if (!HasPrefixId(d.id, package_id))
    return *why = at + "the id must start with '" + package_id + ".'", false;
  for (const auto& [key, dst] : {std::pair<const char*, std::string*>{"title", &d.title},
                                 {"icon", &d.icon},
                                 {"description", &d.description},
                                 {"submenu", &d.submenu}}) {
    if (!j.contains(key)) continue;
    if (!j[key].is_string()) return *why = at + key + " must be a string", false;
    *dst = j[key].get<std::string>();
  }
  if (d.title.empty()) return *why = at + "title is required", false;
  if (!d.icon.empty() && IsIconFile(d.icon)) {
    if (!IsSafeRelativePath(d.icon)) return *why = at + "icon must be a path inside the package", false;
    if (!root.empty()) d.icon = (fs::path(root) / d.icon).lexically_normal().string();
  }
  d.package_id = package_id;
  return ReadImplementation(j, at, &e->implementation_kind, &e->implementation_name, why);
}

/// Every overlay or tool kind the manifest names.
std::set<std::string> KindsOf(const PackageManifest& m) {
  std::set<std::string> kinds;
  for (const OverlayManifestEntry& e : m.overlays) kinds.insert(e.implementation_kind);
  for (const ToolManifestEntry& e : m.tools) kinds.insert(e.implementation_kind);
  return kinds;
}

std::string UniqueName() {
  std::random_device rd;
  std::ostringstream ss;
  ss << std::hex << rd() << rd();
  return ss.str();
}

/// Deletes `dir` only when it sits directly in `parent`, is not a symlink
/// and holds a package manifest.
Status RemovePackageDir(const fs::path& dir, const fs::path& parent) {
  std::error_code ec;
  const fs::path d = fs::weakly_canonical(dir, ec);
  const fs::path p = fs::weakly_canonical(parent, ec);
  if (fs::is_symlink(dir, ec) || d.parent_path() != p ||
      !fs::is_regular_file(d / kPackageManifestName, ec))
    return Status::Error(kInvalidArg, "refusing to delete " + dir.string() +
                                          ": not a package directory in " + parent.string());
  fs::remove_all(d, ec);
  if (ec) return Status::Error(kIoError, "cannot delete " + d.string() + ": " + ec.message());
  return Status::Ok();
}

/// Finds the manifest in a .pgpkg: at the root, or inside its one top-level
/// directory (`top`). Finder's `__MACOSX/` entries are ignored.
Status FindPackageRoot(const std::vector<ZipEntry>& entries, const std::string& source,
                       std::string* top, const ZipEntry** manifest) {
  *top = std::string();
  *manifest = nullptr;
  for (const ZipEntry& e : entries) {
    if (e.name == kPackageManifestName) {
      *manifest = &e;
      return Status::Ok();
    }
  }
  for (const ZipEntry& e : entries) {
    if (e.name.rfind("__MACOSX/", 0) == 0) continue;
    const size_t slash = e.name.find('/');
    if (slash == std::string::npos || (!top->empty() && e.name.compare(0, slash, *top) != 0))
      return Status::Error(kInvalidArg, source + ": not a Peregrine package (expected one directory "
                                                 "holding peregrine-package.json)");
    *top = e.name.substr(0, slash);
  }
  const std::string want = *top + "/" + kPackageManifestName;
  for (const ZipEntry& e : entries)
    if (e.name == want) *manifest = &e;
  if (*manifest == nullptr)
    return Status::Error(kInvalidArg, source + ": not a Peregrine package (no peregrine-package.json)");
  return Status::Ok();
}

bool HasSymlink(const fs::path& root) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_symlink(ec)) return true;
  return false;
}

[[maybe_unused]] const char* Env(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && *v != '\0' ? v : nullptr;
}

}  // namespace

// MARK: Manifest

std::vector<std::string> PackageManifest::CodeKinds() const {
  const std::set<std::string> kinds = KindsOf(*this);
  std::vector<std::string> out;
  if (kinds.count("library")) out.push_back("C library");
  if (kinds.count("native")) out.push_back("native C++ library");
  if (kinds.count("python")) out.push_back("Python");
  return out;
}

Status ParsePackageManifest(const std::string& text, const std::string& source,
                            const std::string& root, bool allow_reserved, PackageManifest* out) {
  auto fail = [&source](const std::string& why) {
    return Status::Error(kInvalidArg, source + ": " + why);
  };
  const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded()) return fail("not valid JSON");
  if (!doc.is_object()) return fail("expected an object");

  PackageManifest m;
  if (!doc.contains("format") || !doc["format"].is_number_integer())
    return fail("format is required");
  m.format = doc["format"].get<int>();
  if (m.format > kPackageFormat)
    return Status::Error(kUnsupported, source + ": package format " + std::to_string(m.format) +
                                           " needs a newer Peregrine");
  if (m.format < 1) return fail("format must be 1 or more");

  std::string why;
  auto str = [&](const json& o, const char* key, std::string* dst, bool required) {
    if (!o.contains(key)) return required ? (why = std::string(key) + " is required", false) : true;
    if (!o[key].is_string()) return why = std::string(key) + " must be a string", false;
    *dst = o[key].get<std::string>();
    if (required && dst->empty()) return why = std::string(key) + " is empty", false;
    return true;
  };
  if (!str(doc, "id", &m.id, true) || !str(doc, "name", &m.name, true) ||
      !str(doc, "version", &m.version, true) || !str(doc, "publisher", &m.publisher, false) ||
      !str(doc, "license", &m.license, false) || !str(doc, "description", &m.description, false) ||
      !str(doc, "python_path", &m.python_path, false))
    return fail(why);
  if (!IsValidPackageId(m.id)) return fail("id '" + m.id + "' is not reverse-DNS (com.example.name)");
  if (!allow_reserved && IsReservedId(m.id))
    return fail("the fv. and peregrine. ids are reserved for packages shipped with Peregrine");
  if (!IsVersion(m.version)) return fail("version '" + m.version + "' must be dotted numbers");
  if (!m.python_path.empty() && !IsSafeRelativePath(m.python_path))
    return fail("python_path must be a path inside the package");

  if (doc.contains("requires")) {
    const json& r = doc["requires"];
    if (!r.is_object()) return fail("requires must be an object");
    if (!str(r, "peregrine", &m.requires_peregrine, false) ||
        !str(r, "python", &m.requires_python, false))
      return fail("requires." + why);
    if (!m.requires_peregrine.empty() && !IsVersion(m.requires_peregrine))
      return fail("requires.peregrine must be dotted numbers");
    if (!m.requires_python.empty() && !IsVersion(m.requires_python))
      return fail("requires.python must be dotted numbers");
    if (r.contains("plugin_api")) {
      if (!r["plugin_api"].is_number_integer() || r["plugin_api"].get<int>() < 1)
        return fail("requires.plugin_api must be a positive integer");
      m.requires_plugin_api = r["plugin_api"].get<int>();
    }
  }

  if (doc.contains("libraries")) {
    const json& libs = doc["libraries"];
    if (!libs.is_object()) return fail("libraries must be an object");
    for (auto it = libs.begin(); it != libs.end(); ++it) {
      if (std::find(std::begin(kPlatformKeys), std::end(kPlatformKeys), it.key()) ==
          std::end(kPlatformKeys))
        return fail("unknown platform '" + it.key() + "' in libraries");
      if (!it.value().is_string() || !IsSafeRelativePath(it.value().get<std::string>()))
        return fail("libraries." + it.key() + " must be a path inside the package");
      m.libraries[it.key()] = it.value().get<std::string>();
    }
  }

  if (doc.contains("overlays")) {
    const json& overlays = doc["overlays"];
    if (!overlays.is_array()) return fail("overlays must be an array");
    for (const json& o : overlays) {
      if (!o.is_object() || !o.contains("id") || !o["id"].is_string()) continue;  // the parser says why
      const std::string id = o["id"].get<std::string>();
      if (!HasPrefixId(id, m.id))
        return fail("overlay '" + id + "': the id must start with '" + m.id + ".'");
      if (o.contains("icon") && o["icon"].is_string()) {
        const std::string icon = o["icon"].get<std::string>();
        if (IsIconFile(icon) && !IsSafeRelativePath(icon))
          return fail("overlay '" + id + "': icon must be a path inside the package");
      }
    }
    // The overlay entries are exactly overlay manifest entries; reuse that parser.
    const Status s = ParseOverlayManifest(json{{"overlays", overlays}}.dump(), source, root, &m.overlays);
    if (!s.ok()) return s;
  }

  if (doc.contains("tools")) {
    if (!doc["tools"].is_array()) return fail("tools must be an array");
    for (const json& t : doc["tools"]) {
      ToolManifestEntry e;
      if (!ParseTool(t, m.id, root, &e, &why)) return fail(why);
      for (const ToolManifestEntry& seen : m.tools)
        if (seen.desc.id == e.desc.id) return fail("tool '" + e.desc.id + "' appears twice");
      m.tools.push_back(std::move(e));
    }
  }

  if (m.overlays.empty() && m.tools.empty()) return fail("the package contributes no overlays or tools");
  const std::set<std::string> kinds = KindsOf(m);
  if (kinds.count("library") && kinds.count("native"))
    return fail("a package holds C or native libraries, not both");
  if ((kinds.count("library") || kinds.count("native")) && m.libraries.empty())
    return fail("compiled entries need a libraries object");
  if (kinds.count("library") && m.requires_plugin_api == 0)
    return fail("C library entries need requires.plugin_api");
  if (kinds.count("python") && m.python_path.empty())
    return fail("Python entries need python_path");

  *out = std::move(m);
  return Status::Ok();
}

Status ReadPackageManifest(const std::string& dir, bool allow_reserved, PackageManifest* out) {
  const fs::path path = fs::path(dir) / kPackageManifestName;
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kNotFound, "cannot read " + path.string());
  std::ostringstream ss;
  ss << in.rdbuf();
  return ParsePackageManifest(ss.str(), path.string(), dir, allow_reserved, out);
}

bool ToolConfig::Excludes(const std::string& id) const {
  for (const std::string& e : exclude) {
    if (e == id) return true;
    if (e.size() > 2 && e.compare(e.size() - 2, 2, ".*") == 0 &&
        id.compare(0, e.size() - 1, e, 0, e.size() - 1) == 0)
      return true;
  }
  return false;
}

Status ParseToolConfig(const std::string& text, const std::string& source, ToolConfig* out) {
  auto fail = [&source](const std::string& why) {
    return Status::Error(kInvalidArg, source + ": " + why);
  };
  const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded()) return fail("not valid JSON");
  if (!doc.is_object()) return fail("expected an object");
  if (doc.contains("format") && (!doc["format"].is_number_integer() || doc["format"].get<int>() > 1))
    return fail("needs a newer Peregrine (format " + doc["format"].dump() + ")");

  ToolConfig c;
  if (doc.contains("exclude")) {
    if (!doc["exclude"].is_array()) return fail("exclude must be an array of tool ids");
    for (const json& e : doc["exclude"]) {
      if (!e.is_string() || e.get<std::string>().empty())
        return fail("exclude must be an array of tool ids");
      c.exclude.push_back(e.get<std::string>());
    }
  }
  if (doc.contains("tools")) {
    if (!doc["tools"].is_array()) return fail("tools must be an array");
    for (const json& t : doc["tools"]) {
      ToolManifestEntry e;
      std::string why;
      if (!ParseTool(t, "user", "", &e, &why)) return fail(why);
      if (e.implementation_kind != "builtin")
        return fail("tool '" + e.desc.id + "': only builtin tools can be added here; " +
                    e.implementation_kind + " tools come in packages");
      for (const ToolManifestEntry& seen : c.tools)
        if (seen.desc.id == e.desc.id) return fail("tool '" + e.desc.id + "' is listed twice");
      e.desc.package_id.clear();
      c.tools.push_back(std::move(e));
    }
  }
  *out = std::move(c);
  return Status::Ok();
}

Status ReadToolConfig(const std::string& path, ToolConfig* out) {
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    *out = ToolConfig();
    return Status::Ok();
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) return Status::Error(kIoError, "cannot read " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ParseToolConfig(ss.str(), path, out);
}

std::string DefaultToolConfigPath() {
  const std::string settings = DefaultUserSettingsPath();
  if (settings.empty()) return std::string();
  return (fs::path(settings).parent_path() / "tools.json").string();
}

int CompareVersions(const std::string& a, const std::string& b) {
  std::istringstream sa(a), sb(b);
  std::string pa, pb;
  for (;;) {
    const bool ha = static_cast<bool>(std::getline(sa, pa, '.'));
    const bool hb = static_cast<bool>(std::getline(sb, pb, '.'));
    if (!ha && !hb) return 0;
    const long na = ha ? std::strtol(pa.c_str(), nullptr, 10) : 0;
    const long nb = hb ? std::strtol(pb.c_str(), nullptr, 10) : 0;
    if (na != nb) return na < nb ? -1 : 1;
  }
}

// MARK: PackageManager

PackageManager::PackageManager(PackageDirs dirs, app::OverlayTypeRegistry& types,
                               ToolRegistry& tools, std::string host_version)
    : dirs_(std::move(dirs)), types_(types), tools_(tools), host_version_(std::move(host_version)) {
  if (!dirs_.user.empty()) dirs_.user = Normal(dirs_.user);
  if (!dirs_.system.empty()) dirs_.system = Normal(dirs_.system);
}

void PackageManager::ReadState() {
  state_ = State();
  if (dirs_.user.empty()) return;
  std::ifstream in(fs::path(dirs_.user) / kStateFile, std::ios::binary);
  if (!in) return;
  std::ostringstream ss;
  ss << in.rdbuf();
  const json doc = json::parse(ss.str(), nullptr, /*allow_exceptions=*/false);
  if (!doc.is_object()) {
    FV_LOG_WARNING("packages: ignoring an unreadable " << kStateFile << " in " << dirs_.user);
    return;
  }
  auto strings = [&doc](const char* key) {
    std::vector<std::string> v;
    if (doc.contains(key) && doc[key].is_array())
      for (const json& s : doc[key])
        if (s.is_string()) v.push_back(s.get<std::string>());
    return v;
  };
  for (const std::string& id : strings("disabled")) state_.disabled.insert(id);
  for (const std::string& d : strings("linked")) state_.linked.push_back(Normal(d));
  for (const std::string& d : strings("pending_removal")) state_.pending_removal.insert(Normal(d));
}

Status PackageManager::WriteState() const {
  if (dirs_.user.empty()) return Status::Error(kUnsupported, "no user packages directory");
  std::error_code ec;
  fs::create_directories(dirs_.user, ec);
  json doc = {{"disabled", state_.disabled},
              {"linked", state_.linked},
              {"pending_removal", state_.pending_removal}};
  const fs::path path = fs::path(dirs_.user) / kStateFile;
  const fs::path tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << doc.dump(2) << "\n";
    if (!out) return Status::Error(kIoError, "cannot write " + tmp.string());
  }
  fs::rename(tmp, path, ec);
  if (ec) return Status::Error(kIoError, "cannot write " + path.string() + ": " + ec.message());
  return Status::Ok();
}

void PackageManager::ApplyPending(std::vector<std::string>* warnings) {
  if (dirs_.user.empty()) return;
  auto warn = [warnings](const std::string& w) {
    FV_LOG_WARNING("packages: " << w);
    if (warnings != nullptr) warnings->push_back(w);
  };
  const fs::path user(dirs_.user);
  std::error_code ec;
  // An install interrupted mid-way leaves only staging debris.
  fs::remove_all(user / kStagingDir, ec);

  const bool had_pending = !state_.pending_removal.empty();
  for (const std::string& dir : state_.pending_removal) {
    auto linked = std::find(state_.linked.begin(), state_.linked.end(), dir);
    if (linked != state_.linked.end()) {
      state_.linked.erase(linked);
      continue;
    }
    const Status s = RemovePackageDir(dir, user);
    if (!s.ok()) warn(s.message);
  }
  state_.pending_removal.clear();

  const fs::path pending = user / kPendingDir;
  if (fs::is_directory(pending, ec)) {
    for (fs::directory_iterator it(pending, ec), end; !ec && it != end; it.increment(ec)) {
      if (!it->is_directory(ec)) continue;
      const fs::path target = user / it->path().filename();
      if (fs::exists(target, ec)) {
        const Status s = RemovePackageDir(target, user);
        if (!s.ok()) {
          warn(s.message);
          continue;
        }
      }
      std::error_code mv;
      fs::rename(it->path(), target, mv);
      if (mv) warn("cannot move the upgrade of " + target.filename().string() + " into place: " + mv.message());
    }
  }
  if (had_pending) {
    const Status s = WriteState();
    if (!s.ok()) warn(s.message);
  }
}

void PackageManager::Rescan(std::vector<std::string>* warnings) {
  auto warn = [warnings](const std::string& w) {
    FV_LOG_WARNING("packages: " << w);
    if (warnings != nullptr) warnings->push_back(w);
  };
  std::vector<PackageInfo> found;
  auto add = [&](const fs::path& dir, PackageLocation loc) {
    PackageInfo p;
    p.dir = Normal(dir);
    p.location = loc;
    const Status s = ReadPackageManifest(p.dir, loc == PackageLocation::kSystem, &p.manifest);
    if (!s.ok()) {
      p.id = fs::path(p.dir).filename().string();
      p.manifest = PackageManifest();
      p.state = PackageState::kFailed;
      p.reason = s.message;
      warn(s.message);
    } else {
      p.id = p.manifest.id;
      if (loc != PackageLocation::kLinked && fs::path(p.dir).filename() != p.id) {
        p.state = PackageState::kFailed;
        p.reason = p.dir + ": the directory must be named after the package id '" + p.id + "'";
        warn(p.reason);
      }
    }
    found.push_back(std::move(p));
  };
  auto add_children = [&](const std::string& root, PackageLocation loc) {
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return;
    std::vector<fs::path> dirs;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
      if (it->is_directory(ec) && it->path().filename().string().rfind('.', 0) != 0)
        dirs.push_back(it->path());
    std::sort(dirs.begin(), dirs.end());
    for (const fs::path& d : dirs) add(d, loc);
  };
  for (const std::string& d : state_.linked) add(d, PackageLocation::kLinked);
  add_children(dirs_.user, PackageLocation::kUser);
  add_children(dirs_.system, PackageLocation::kSystem);

  std::map<std::string, std::string> winners;  // id → dir
  for (PackageInfo& p : found) {
    if (p.state == PackageState::kFailed) continue;
    auto w = winners.find(p.id);
    if (w != winners.end()) {
      p.state = PackageState::kShadowed;
      p.reason = "overridden by " + w->second;
      continue;
    }
    winners.emplace(p.id, p.dir);
    p.enabled = state_.disabled.count(p.id) == 0;
    p.pending_removal = state_.pending_removal.count(p.dir) != 0;
    std::error_code ec;
    const fs::path staged = fs::path(dirs_.user) / kPendingDir / p.id;
    PackageManifest next;
    if (!dirs_.user.empty() && fs::is_directory(staged, ec) &&
        ReadPackageManifest(staged.string(), false, &next).ok())
      p.pending_version = next.version;
    auto outcome = outcomes_.find(p.id);
    if (loaded_.count(p.id)) {
      p.state = PackageState::kLoaded;
    } else if (outcome != outcomes_.end() && outcome->second.dir == p.dir) {
      p.state = outcome->second.state;
      p.reason = outcome->second.reason;
    } else {
      p.state = p.enabled ? PackageState::kNotLoaded : PackageState::kDisabled;
    }
  }
  packages_ = std::move(found);
}

void PackageManager::Scan(std::vector<std::string>* warnings) {
  ReadState();
  ApplyPending(warnings);
  Rescan(warnings);
}

void PackageManager::Load(PackageInfo& p) {
  if (p.state != PackageState::kNotLoaded || loaded_.count(p.id)) return;
  const PackageManifest& m = p.manifest;
  auto settle = [this, &p](PackageState state, const std::string& why) {
    p.state = state;
    p.reason = why;
    outcomes_[p.id] = Outcome{p.dir, state, why};
    FV_LOG_WARNING("packages: " << p.id << ": " << why);
  };
  if (m.requires_plugin_api != 0 && m.requires_plugin_api != kPluginApiMajor)
    return settle(PackageState::kUnavailable,
                  "built for plugin API " + std::to_string(m.requires_plugin_api) +
                      "; this Peregrine hosts API " + std::to_string(kPluginApiMajor));
  if (!host_version_.empty() && !m.requires_peregrine.empty() &&
      CompareVersions(m.requires_peregrine, host_version_) > 0)
    return settle(PackageState::kUnavailable,
                  "needs Peregrine " + m.requires_peregrine + " or later; this is " + host_version_);

  // Everything is resolved before anything registers, so a package loads whole or not at all.
  const PluginLibrary* lib = nullptr;
  auto uses = [&m](const char* kind) {
    return std::any_of(m.overlays.begin(), m.overlays.end(),
                       [kind](const OverlayManifestEntry& e) { return e.implementation_kind == kind; }) ||
           std::any_of(m.tools.begin(), m.tools.end(),
                       [kind](const ToolManifestEntry& e) { return e.implementation_kind == kind; });
  };
  if (uses("library")) {
    const std::string rel = SelectPluginLibrary(m.libraries);
    const std::vector<std::string> keys = PluginPlatformKeys();
    if (rel.empty())
      return settle(PackageState::kUnavailable,
                    "no library for this platform (" + (keys.empty() ? "unknown" : keys.front()) + ")");
    const Status s = LoadPluginLibrary((fs::path(p.dir) / rel).string(), p.dir, host_version_, &lib);
    if (!s.ok())
      return settle(s.code == kUnsupported ? PackageState::kUnavailable : PackageState::kFailed,
                    s.message);
  }
  const NativeLibrary* native = nullptr;
  if (uses("native")) {
    const std::string rel = SelectPluginLibrary(m.libraries);
    const std::vector<std::string> keys = PluginPlatformKeys();
    if (rel.empty())
      return settle(PackageState::kUnavailable,
                    "no library for this platform (" + (keys.empty() ? "unknown" : keys.front()) + ")");
    const Status s = LoadNativeLibrary((fs::path(p.dir) / rel).string(), p.dir, &native);
    if (!s.ok())
      return settle(s.code == kUnsupported ? PackageState::kUnavailable : PackageState::kFailed,
                    s.message);
  }
  PythonBridge* python = nullptr;
  if (uses("python")) {
    if (!m.requires_python.empty() && CompareVersions(m.requires_python, kMinimumPython) < 0)
      return settle(PackageState::kUnavailable, "written for Python " + m.requires_python +
                                                    "; Peregrine needs " + kMinimumPython +
                                                    " or later");
    const Status s = LoadPythonBridge(&python);
    if (!s.ok()) return settle(PackageState::kUnavailable, s.message);
    if (!m.requires_python.empty() && CompareVersions(m.requires_python, python->Version()) > 0)
      FV_LOG_WARNING("packages: " << p.id << " was written for Python " << m.requires_python
                                  << "; this is " << python->Version());
    const Status ps = python->AddPath((fs::path(p.dir) / m.python_path).string());
    if (!ps.ok()) return settle(PackageState::kFailed, ps.message);
  }

  std::vector<app::OverlayTypeDesc> types;
  for (const OverlayManifestEntry& e : m.overlays) {
    const std::string& id = e.desc.id;
    app::OverlayTypeDesc d = e.desc;
    if (e.implementation_kind == "builtin") {
      const BuiltinOverlay* impl = FindBuiltinOverlay(e.implementation_name);
      if (impl == nullptr)
        return settle(PackageState::kFailed, "'" + id + "': no builtin overlay named '" +
                                                 e.implementation_name + "'");
      d.factory = impl->factory;
      d.editor_factory = impl->editor_factory;
    } else if (e.implementation_kind == "library") {
      const fv_overlay_class* cls = lib->FindOverlay(e.implementation_name);
      if (cls == nullptr)
        return settle(PackageState::kFailed, "'" + id + "': the library has no overlay class '" +
                                                 e.implementation_name + "'");
      const std::string name = e.desc.display_name;
      d.factory = [lib, cls, id, name] { return CreateLibraryOverlay(*lib, *cls, id, name); };
    } else if (e.implementation_kind == "native") {
      const NativeOverlay* impl = native->FindOverlay(e.implementation_name);
      if (impl == nullptr)
        return settle(PackageState::kFailed, "'" + id + "': the library has no native overlay '" +
                                                 e.implementation_name + "'");
      d.factory = impl->factory;
      if (impl->editor_factory) {
        const NativeEditorContext ctx{editors_};
        d.editor_factory = [impl, ctx] { return impl->editor_factory(ctx); };
      }
    } else if (e.implementation_kind == "python") {
      const Status s = python->ResolveOverlay(e.implementation_name, e.desc.display_name, &d.factory);
      if (!s.ok()) return settle(PackageState::kFailed, "'" + id + "': " + s.message);
    } else {
      return settle(PackageState::kUnavailable,
                    e.implementation_kind + " overlays are not supported by this Peregrine yet");
    }
    if (types_.Find(id) != nullptr)
      return settle(PackageState::kFailed, "overlay type '" + id + "' is already registered");
    if (e.desc.file) {
      const app::OverlayTypeDesc* owner = types_.FindByExtension(e.desc.file->default_extension);
      if (owner != nullptr)
        return settle(PackageState::kFailed, "." + e.desc.file->default_extension +
                                                 " files already open as " + owner->display_name);
    }
    types.push_back(std::move(d));
  }
  std::vector<ToolRun> runs;
  for (const ToolManifestEntry& e : m.tools) {
    const std::string& id = e.desc.id;
    if (e.implementation_kind == "builtin") {
      const ToolRun* run = FindBuiltinTool(e.implementation_name);
      if (run == nullptr)
        return settle(PackageState::kFailed,
                      "'" + id + "': no builtin tool named '" + e.implementation_name + "'");
      runs.push_back(*run);
    } else if (e.implementation_kind == "library") {
      const fv_tool_class* cls = lib->FindTool(e.implementation_name);
      if (cls == nullptr)
        return settle(PackageState::kFailed, "'" + id + "': the library has no tool class '" +
                                                 e.implementation_name + "'");
      runs.push_back(MakeLibraryTool(*lib, *cls));
    } else if (e.implementation_kind == "native") {
      const ToolRun* run = native->FindTool(e.implementation_name);
      if (run == nullptr)
        return settle(PackageState::kFailed, "'" + id + "': the library has no native tool '" +
                                                 e.implementation_name + "'");
      runs.push_back(*run);
    } else if (e.implementation_kind == "python") {
      ToolRun run;
      const Status s = python->ResolveTool(e.implementation_name, p.dir, &run);
      if (!s.ok()) return settle(PackageState::kFailed, "'" + id + "': " + s.message);
      runs.push_back(std::move(run));
    } else {
      return settle(PackageState::kUnavailable,
                    e.implementation_kind + " tools are not supported by this Peregrine yet");
    }
    if (tools_.Find(id) != nullptr)
      return settle(PackageState::kFailed, "tool '" + id + "' is already registered");
  }

  for (app::OverlayTypeDesc& d : types) {
    const Status s = types_.Register(std::move(d));
    if (!s.ok()) return settle(PackageState::kFailed, s.message);
  }
  for (size_t i = 0; i < m.tools.size(); ++i) {
    ToolDesc d = m.tools[i].desc;
    d.run = runs[i];
    const Status s = tools_.Register(std::move(d));
    if (!s.ok()) return settle(PackageState::kFailed, s.message);
  }
  p.state = PackageState::kLoaded;
  p.reason.clear();
  loaded_[p.id] = p.dir;
  outcomes_.erase(p.id);
  FV_LOG_INFO("packages: loaded " << p.id << " " << m.version << " from " << p.dir);
}

void PackageManager::LoadAll() {
  for (PackageInfo& p : packages_) Load(p);
}

PackageInfo* PackageManager::FindMutable(const std::string& id) {
  for (PackageInfo& p : packages_)
    if (p.id == id && p.state != PackageState::kShadowed) return &p;
  return nullptr;
}

const PackageInfo* PackageManager::Find(const std::string& id) const {
  return const_cast<PackageManager*>(this)->FindMutable(id);
}

bool PackageManager::RestartPending() const {
  return std::any_of(packages_.begin(), packages_.end(), [](const PackageInfo& p) {
    return p.pending_removal || !p.pending_version.empty() ||
           (p.state == PackageState::kLoaded && !p.enabled);
  });
}

Status PackageManager::Inspect(const std::string& path, PackageManifest* out) const {
  std::error_code ec;
  if (fs::is_directory(path, ec)) return ReadPackageManifest(path, false, out);
  ZipReader zip;
  Status s = zip.Open(path);
  if (!s.ok()) return s;
  std::string top;
  const ZipEntry* manifest = nullptr;
  s = FindPackageRoot(zip.entries(), path, &top, &manifest);
  if (!s.ok()) return s;
  const fs::path tmp = fs::temp_directory_path(ec) / ("fv-pgpkg-" + UniqueName() + ".json");
  s = zip.Extract(*manifest, tmp.string());
  if (!s.ok()) return s;
  std::ostringstream ss;
  ss << std::ifstream(tmp, std::ios::binary).rdbuf();
  fs::remove(tmp, ec);
  return ParsePackageManifest(ss.str(), path, std::string(), false, out);
}

Status PackageManager::Install(const std::string& path, InstallResult* result) {
  std::error_code ec;
  if (fs::is_directory(path, ec)) return Link(path, result);
  return InstallZip(path, result);
}

Status PackageManager::InstallZip(const std::string& path, InstallResult* result) {
  if (dirs_.user.empty()) return Status::Error(kUnsupported, "no user packages directory");
  ZipReader zip;
  Status s = zip.Open(path);
  if (!s.ok()) return s;
  std::string top;
  const ZipEntry* manifest_entry = nullptr;
  s = FindPackageRoot(zip.entries(), path, &top, &manifest_entry);
  if (!s.ok()) return s;

  const fs::path user(dirs_.user);
  const fs::path staging = user / kStagingDir / UniqueName();
  std::error_code ec;
  fs::create_directories(staging, ec);
  if (ec) return Status::Error(kIoError, "cannot create " + staging.string() + ": " + ec.message());
  struct Cleanup {
    fs::path dir;
    ~Cleanup() {
      std::error_code e;
      fs::remove_all(dir, e);
    }
  } cleanup{staging};

  s = zip.ExtractAll(staging.string());
  if (!s.ok()) return s;
  fs::remove_all(staging / "__MACOSX", ec);
  const fs::path root = top.empty() ? staging : staging / top;
  PackageManifest m;
  s = ReadPackageManifest(root.string(), false, &m);
  if (!s.ok()) return Status::Error(s.code, path + ": " + s.message);
  if (!top.empty() && top != m.id)
    return Status::Error(kInvalidArg, path + ": the package directory is named '" + top +
                                          "' but its id is '" + m.id + "'");
  if (HasSymlink(root)) return Status::Error(kInvalidArg, path + ": a package may not hold symbolic links");
  for (const PackageInfo& p : packages_)
    if (p.id == m.id && p.location == PackageLocation::kLinked)
      return Status::Error(kInvalidArg, "'" + m.id + "' is linked from " + p.dir + "; unlink it first");

  *result = InstallResult();
  result->id = m.id;
  if (const PackageInfo* current = Find(m.id)) result->replaced_version = current->manifest.version;

  if (loaded_.count(m.id)) {
    const fs::path pending_root = user / kPendingDir;
    const fs::path pending = pending_root / m.id;
    if (fs::exists(pending, ec)) {
      s = RemovePackageDir(pending, pending_root);
      if (!s.ok()) return s;
    }
    fs::create_directories(pending_root, ec);
    fs::rename(root, pending, ec);
    if (ec) return Status::Error(kIoError, "cannot stage " + m.id + ": " + ec.message());
    Rescan(nullptr);
    return Status::Ok();
  }

  const fs::path target = user / m.id;
  if (fs::exists(target, ec)) {
    s = RemovePackageDir(target, user);
    if (!s.ok()) return s;
  }
  fs::rename(root, target, ec);
  if (ec) return Status::Error(kIoError, "cannot install " + m.id + ": " + ec.message());
  state_.disabled.erase(m.id);
  s = WriteState();
  if (!s.ok()) return s;
  Rescan(nullptr);
  if (PackageInfo* p = FindMutable(m.id)) {
    Load(*p);
    result->live = p->state == PackageState::kLoaded;
  }
  return Status::Ok();
}

Status PackageManager::Link(const std::string& dir, InstallResult* result) {
  if (dirs_.user.empty()) return Status::Error(kUnsupported, "no user packages directory");
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return Status::Error(kNotFound, dir + " is not a directory");
  const std::string abs = Normal(dir);
  PackageManifest m;
  Status s = ReadPackageManifest(abs, false, &m);
  if (!s.ok()) return s;
  for (const PackageInfo& p : packages_)
    if (p.id == m.id)
      return Status::Error(kInvalidArg, "'" + m.id + "' is already installed in " + p.dir +
                                            "; remove it first");
  state_.linked.push_back(abs);
  state_.disabled.erase(m.id);
  s = WriteState();
  if (!s.ok()) return s;
  Rescan(nullptr);
  *result = InstallResult();
  result->id = m.id;
  if (PackageInfo* p = FindMutable(m.id)) {
    Load(*p);
    result->live = p->state == PackageState::kLoaded;
  }
  return Status::Ok();
}

Status PackageManager::SetEnabled(const std::string& id, bool enabled) {
  PackageInfo* p = FindMutable(id);
  if (p == nullptr) return Status::Error(kNotFound, "no package '" + id + "'");
  if (enabled) {
    state_.disabled.erase(id);
  } else {
    state_.disabled.insert(id);
  }
  const Status s = WriteState();
  if (!s.ok()) return s;
  p->enabled = enabled;
  if (enabled && p->state == PackageState::kDisabled) {
    p->state = PackageState::kNotLoaded;
    Load(*p);
  } else if (!enabled && p->state == PackageState::kNotLoaded) {
    p->state = PackageState::kDisabled;
  }
  return Status::Ok();
}

Status PackageManager::Remove(const std::string& id) {
  PackageInfo* p = FindMutable(id);
  if (p == nullptr) return Status::Error(kNotFound, "no package '" + id + "'");
  const std::string name = p->manifest.name.empty() ? id : p->manifest.name;
  if (p->location == PackageLocation::kSystem)
    return Status::Error(kInvalidArg, name + " ships with Peregrine; disable it instead");
  if (dirs_.user.empty()) return Status::Error(kUnsupported, "no user packages directory");
  const fs::path user(dirs_.user);
  std::error_code ec;
  const fs::path staged = user / kPendingDir / id;
  if (fs::exists(staged, ec)) {
    const Status s = RemovePackageDir(staged, user / kPendingDir);
    if (!s.ok()) return s;
  }

  // The disabled choice goes with the last copy, so a reinstall starts enabled.
  const bool other_copy = std::any_of(packages_.begin(), packages_.end(), [p, &id](const PackageInfo& o) {
    return &o != p && o.id == id;
  });
  if (!other_copy) state_.disabled.erase(id);

  auto loaded = loaded_.find(id);
  if (loaded != loaded_.end() && loaded->second == p->dir) {
    state_.pending_removal.insert(p->dir);
    const Status s = WriteState();
    if (!s.ok()) return s;
    p->pending_removal = true;
    p->pending_version.clear();
    return Status::Ok();
  }
  if (p->location == PackageLocation::kLinked) {
    state_.linked.erase(std::remove(state_.linked.begin(), state_.linked.end(), p->dir),
                        state_.linked.end());
  } else {
    const Status s = RemovePackageDir(p->dir, user);
    if (!s.ok()) return s;
  }
  const Status s = WriteState();
  Rescan(nullptr);
  return s;
}

std::string DefaultUserPackageDir() {
#if defined(_WIN32) || defined(__APPLE__)
  const std::string settings = DefaultUserSettingsPath();
  if (settings.empty()) return std::string();
  return (fs::path(settings).parent_path() / "packages").string();
#else
  if (const char* xdg = Env("XDG_DATA_HOME")) return (fs::path(xdg) / "peregrine" / "packages").string();
  if (const char* home = Env("HOME"))
    return (fs::path(home) / ".local" / "share" / "peregrine" / "packages").string();
  return std::string();
#endif
}

}  // namespace desk
}  // namespace fv
