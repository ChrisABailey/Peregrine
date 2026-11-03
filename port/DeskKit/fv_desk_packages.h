// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_packages.h — installable overlay and tool packages
/// (port/extensions-plan.md §1).
///
/// A package is a directory named after its id, holding
/// `peregrine-package.json` plus its libraries, Python code and resources; a
/// `.pgpkg` is a zip of one. Packages are found in three places: shipped with
/// the app (system), the user's packages directory, and directories linked in
/// place for development. When two hold the same id, linked beats user beats
/// system. Installing and enabling apply at once; removing, disabling and
/// upgrading a loaded package apply at the next launch, because a loaded
/// package's code is never unloaded.
///
/// The user directory also holds `.state.json` (disabled ids, linked
/// directories, removals pending), `.pending/<id>` (upgrades waiting for the
/// next launch) and `.staging/` (installs in progress).
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "fv_desk_overlay_manifest.h"
#include "fv_desk_tools.h"
#include "fvkit/app/type_registry.h"

namespace fv {
namespace app {
class EditorManager;
}
namespace desk {

constexpr int kPackageFormat = 1;   ///< the highest `format` this build reads
constexpr int kPluginApiMajor = 1;  ///< the C plugin API (`fv_plugin.h`) this build hosts
constexpr char kPackageManifestName[] = "peregrine-package.json";

/// One `tools` entry: a ToolDesc without its `run`, and where that comes from.
struct ToolManifestEntry {
  ToolDesc desc;
  std::string implementation_kind;  ///< "builtin", "library", "native" or "python"
  std::string implementation_name;
};

/// A parsed `peregrine-package.json`.
struct PackageManifest {
  int format = 0;
  std::string id;
  std::string name;
  std::string version;
  std::string publisher;
  std::string license;
  std::string description;
  std::string requires_peregrine;  ///< minimum host version; empty for any
  int requires_plugin_api = 0;     ///< 0 when no entry is a C library
  std::string requires_python;     ///< "3.12"; empty when no entry is Python
  std::map<std::string, std::string> libraries;  ///< platform key → path in the package
  std::string python_path;                       ///< relative to the package root
  std::vector<OverlayManifestEntry> overlays;
  std::vector<ToolManifestEntry> tools;

  /// The kinds of code the package runs, as an install confirmation names
  /// them ("C library", "native C++ library", "Python"); empty when every
  /// entry is builtin.
  std::vector<std::string> CodeKinds() const;
};

/// Parses and validates a package manifest. `root` is the package directory;
/// icon files resolve against it. `allow_reserved` permits the `fv.` and
/// `peregrine.` id prefixes, which only packages shipped with the app may use.
/// All or nothing: on error `out` is unchanged.
Status ParsePackageManifest(const std::string& text, const std::string& source,
                            const std::string& root, bool allow_reserved, PackageManifest* out);
/// Reads `dir/peregrine-package.json`.
Status ReadPackageManifest(const std::string& dir, bool allow_reserved, PackageManifest* out);

/// The user's Tools menu choices, from `tools.json` beside the user settings:
/// `{"format": 1, "exclude": [ids], "tools": [entries]}`. An exclude entry is
/// a tool id or a prefix ending in ".*" ("com.example.*"); it hides package
/// tools and added tools alike. A `tools` entry has a package tool's shape,
/// an id starting with "user." and a builtin implementation.
struct ToolConfig {
  std::vector<std::string> exclude;
  std::vector<ToolManifestEntry> tools;

  /// True when `id` matches an exclude entry.
  bool Excludes(const std::string& id) const;
};

/// Parses a tools.json. All or nothing: on error `out` is unchanged.
Status ParseToolConfig(const std::string& text, const std::string& source, ToolConfig* out);
/// Reads the file at `path`; a missing file is an empty configuration.
Status ReadToolConfig(const std::string& path, ToolConfig* out);
/// `tools.json` beside `DefaultUserSettingsPath()`; empty when no home is known.
std::string DefaultToolConfigPath();

/// Compares dotted numeric versions ("1.10" > "1.9"; a missing component is 0).
/// Negative, zero or positive, as strcmp.
int CompareVersions(const std::string& a, const std::string& b);

enum class PackageLocation { kSystem, kUser, kLinked };

enum class PackageState {
  kNotLoaded,    ///< found and enabled, not yet loaded
  kLoaded,       ///< its overlays and tools are registered in this process
  kDisabled,     ///< the user turned it off
  kShadowed,     ///< another location holds the same id and wins
  kFailed,       ///< the manifest or a registration failed; `reason` says which
  kUnavailable,  ///< needs something this Peregrine lacks; `reason` says what
};

struct PackageInfo {
  std::string id;            ///< the manifest's id, or the directory name when it failed
  PackageManifest manifest;  ///< empty when the manifest failed to parse
  std::string dir;
  PackageLocation location = PackageLocation::kUser;
  PackageState state = PackageState::kNotLoaded;
  std::string reason;
  bool enabled = true;           ///< the user's choice; a loaded package stays loaded until relaunch
  bool pending_removal = false;  ///< removed at the next launch
  std::string pending_version;   ///< non-empty: this version replaces it at the next launch
};

struct PackageDirs {
  std::string system;  ///< shipped with the app; may be empty
  std::string user;    ///< installs land here; empty disables installing
};

struct InstallResult {
  std::string id;
  bool live = false;             ///< registered now; false: at the next launch
  std::string replaced_version;  ///< the version an upgrade replaces
};

/// Finds, loads, installs and removes packages. The registries must outlive
/// it; package contributions are registered in them and never removed.
class PackageManager {
 public:
  /// `host_version` is checked against `requires.peregrine`; empty skips the check.
  PackageManager(PackageDirs dirs, app::OverlayTypeRegistry& types, ToolRegistry& tools,
                 std::string host_version = std::string());

  /// Applies pending removals and upgrades, then reads every location. Call
  /// once at startup, before `LoadAll`. Problems are appended to `warnings`.
  void Scan(std::vector<std::string>* warnings);
  /// Registers the overlays and tools of every enabled package that wins its id.
  void LoadAll();
  /// The editor manager native editor factories are given; set before loading.
  void SetEditorManager(app::EditorManager* editors) { editors_ = editors; }

  const std::vector<PackageInfo>& packages() const { return packages_; }
  /// The copy of `id` that wins, or null.
  const PackageInfo* Find(const std::string& id) const;
  /// True while some change waits for the next launch.
  bool RestartPending() const;
  const PackageDirs& dirs() const { return dirs_; }

  /// Reads the manifest of a .pgpkg or a package directory without installing it.
  Status Inspect(const std::string& path, PackageManifest* out) const;
  /// Installs a .pgpkg into the user directory, or links a directory (`Link`).
  /// A package whose id is loaded is staged and replaces it at the next
  /// launch; otherwise it is loaded now.
  Status Install(const std::string& path, InstallResult* result);
  /// Uses the package directory `dir` in place. Refused when the id is
  /// already installed anywhere.
  Status Link(const std::string& dir, InstallResult* result);
  /// Records the choice. Enabling loads the package now; disabling a loaded
  /// one applies at the next launch.
  Status SetEnabled(const std::string& id, bool enabled);
  /// Removes the winning copy of `id`: a system package is refused, a loaded
  /// one is removed at the next launch, any other at once. Only a directory
  /// inside the user directory that holds a manifest is ever deleted.
  Status Remove(const std::string& id);

 private:
  struct State {
    std::set<std::string> disabled;         ///< ids
    std::vector<std::string> linked;        ///< absolute directories
    std::set<std::string> pending_removal;  ///< absolute directories
  };

  void ReadState();
  Status WriteState() const;
  void ApplyPending(std::vector<std::string>* warnings);
  /// Rebuilds `packages_` from disk, keeping this process's load results.
  void Rescan(std::vector<std::string>* warnings);
  void Load(PackageInfo& p);
  Status InstallZip(const std::string& path, InstallResult* result);
  PackageInfo* FindMutable(const std::string& id);

  PackageDirs dirs_;
  app::OverlayTypeRegistry& types_;
  ToolRegistry& tools_;
  std::string host_version_;
  app::EditorManager* editors_ = nullptr;
  State state_;
  std::vector<PackageInfo> packages_;
  std::map<std::string, std::string> loaded_;  ///< id → directory loaded from
  struct Outcome {
    std::string dir;
    PackageState state;
    std::string reason;
  };
  std::map<std::string, Outcome> outcomes_;  ///< id → failed or unavailable load
};

/// The per-user packages directory: `packages/` beside the user settings
/// file on macOS and Windows, `$XDG_DATA_HOME/peregrine/packages` on Linux.
/// Empty when no home directory is known.
std::string DefaultUserPackageDir();

}  // namespace desk
}  // namespace fv
