// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_native_plugin.h — the host side of the `native` plugin kind
/// (`port/sdk/include/peregrine/fv_native.h`, port/extensions-plan.md §2b).
///
/// A native library is loaded only when the build id embedded in its file is
/// this Peregrine's, so a plugin built for another release never runs. A
/// loaded library is never unloaded.
#pragma once

#include <string>

#include "peregrine/fv_native.h"

namespace fv {
namespace desk {

/// One loaded native library and the classes its `fv_native_register` added.
struct NativeLibrary {
  std::string path;
  NativeRegistrar registry;

  explicit NativeLibrary(std::string package_dir) : registry(std::move(package_dir)) {}
  const NativeOverlay* FindOverlay(const std::string& name) const;
  const ToolRun* FindTool(const std::string& name) const;
};

/// This Peregrine's build id (FV_SDK_BUILD_ID).
const std::string& NativeHostBuildId();

/// Reads the build id a native plugin file carries, without loading it.
/// kNotFound when the file has none; kInvalidArg when its slices disagree.
Status ReadNativeBuildId(const std::string& path, std::string* id);

/// Loads `path` (once; later calls answer from the first) and runs its
/// `fv_native_register`. kUnsupported, before anything is loaded, when the
/// file's build id is not this Peregrine's; kIoError when it will not load;
/// kInternal when registration fails or throws.
Status LoadNativeLibrary(const std::string& path, const std::string& package_dir,
                         const NativeLibrary** out);

}  // namespace desk
}  // namespace fv
