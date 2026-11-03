// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_library_plugin.h — the host side of the C plugin API
/// (`port/sdk/include/peregrine/fv_plugin.h`, port/extensions-plan.md §2c).
///
/// Loads a package's C library once per path and never unloads it, and
/// adapts its overlay and tool classes to `fv::Overlay` and `ToolRun`. The
/// overlay adapter offers each capability (properties, persistence, hit test)
/// only when the class supplies the functions behind it.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fv_desk_tools.h"
#include "fvkit/overlay/overlay.h"
#include "peregrine/fv_plugin.h"

namespace fv {
namespace desk {

/// One loaded plugin library. Lives for the rest of the process.
struct PluginLibrary {
  std::string path;
  std::string package_dir;
  std::string host_version;
  fv_host_api host{};  ///< handed to the plugin; points into the strings above
  fv_plugin_api api{};

  /// The class named `name`, or null.
  const fv_overlay_class* FindOverlay(const std::string& name) const;
  const fv_tool_class* FindTool(const std::string& name) const;
};

/// The `libraries` keys this machine accepts, most specific first
/// ("macos-arm64", "macos").
std::vector<std::string> PluginPlatformKeys();
/// The path `libraries` lists for this machine, or empty.
std::string SelectPluginLibrary(const std::map<std::string, std::string>& libraries);

/// Loads `path` (once; later calls answer from the first) and calls its entry
/// point. kUnsupported when the plugin was built for another major API
/// version; kNotFound or kIoError when it will not load; whatever the entry
/// point returns otherwise. A library that loaded is never closed, even when
/// refused.
Status LoadPluginLibrary(const std::string& path, const std::string& package_dir,
                         const std::string& host_version, const PluginLibrary** out);

/// A new overlay of `cls`, named `display_name`; null when the plugin's
/// `create` fails (the reason is logged).
std::shared_ptr<Overlay> CreateLibraryOverlay(const PluginLibrary& lib,
                                              const fv_overlay_class& cls,
                                              const std::string& type_id,
                                              const std::string& display_name);

/// A ToolRun that calls `cls.run` with a context over the Desk.
ToolRun MakeLibraryTool(const PluginLibrary& lib, const fv_tool_class& cls);

}  // namespace desk
}  // namespace fv
