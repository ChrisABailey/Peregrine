// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_native.h — the `native` plugin kind: C++ against fvkit and DeskKit,
/// locked to one Peregrine build (port/extensions-plan.md §2b).
///
/// A plugin links libperegrine_core (`Peregrine::core`) and defines two
/// symbols with the macros below:
///
///     FV_NATIVE_PLUGIN_BUILD_ID()
///     FV_NATIVE_EXPORT int fv_native_register(fv::desk::NativeRegistrar* r) {
///       r->AddOverlay("rings", {...});
///       return 0;
///     }
///
/// The host reads the build id out of the file before loading it and refuses
/// a plugin built for any other Peregrine, so none of its code runs.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "fv_desk_tools.h"
#include "fvkit/app/editor.h"
#include "fvkit/overlay/overlay.h"

#if defined(_WIN32)
#define FV_NATIVE_EXPORT extern "C" __declspec(dllexport)
#else
#define FV_NATIVE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// Precedes the build id in the plugin's binary; the host searches for it.
#define FV_NATIVE_BUILD_ID_MARKER "fv-native-build-id="

/// Defines `fv_native_build_id()`, which answers the marker and the id of the
/// Peregrine the plugin is compiled against (FV_SDK_BUILD_ID, set by
/// `Peregrine::core`).
#define FV_NATIVE_PLUGIN_BUILD_ID()                                   \
  FV_NATIVE_EXPORT const char* fv_native_build_id() {                 \
    return FV_NATIVE_BUILD_ID_MARKER FV_SDK_BUILD_ID;                 \
  }

namespace fv {
namespace desk {

/// What a native editor factory is given. `editors` is the Desk's editor
/// manager, whose `edited()` is the overlay being edited; null when the
/// package loads without a Desk (headless tests).
struct NativeEditorContext {
  app::EditorManager* editors = nullptr;
};

/// One overlay class: the same two factories a built-in overlay has, except
/// that the editor factory is told where to find the edited overlay.
struct NativeOverlay {
  std::function<std::shared_ptr<Overlay>()> factory;  ///< required
  std::function<std::unique_ptr<app::OverlayEditor>(const NativeEditorContext&)>
      editor_factory;  ///< may be null
};

/// What `fv_native_register` fills: the classes the package manifest refers
/// to by name under `implementation.native`.
class NativeRegistrar {
 public:
  explicit NativeRegistrar(std::string package_dir) : package_dir_(std::move(package_dir)) {}

  /// kInvalidArg for an empty name, a null factory or a name already added.
  Status AddOverlay(const std::string& name, NativeOverlay impl);
  /// kInvalidArg for an empty name, a null `run` or a name already added.
  Status AddTool(const std::string& name, ToolRun run);

  /// The package's root directory; resources resolve against it.
  const std::string& package_dir() const { return package_dir_; }
  const std::map<std::string, NativeOverlay>& overlays() const { return overlays_; }
  const std::map<std::string, ToolRun>& tools() const { return tools_; }

 private:
  std::string package_dir_;
  std::map<std::string, NativeOverlay> overlays_;
  std::map<std::string, ToolRun> tools_;
};

}  // namespace desk
}  // namespace fv
