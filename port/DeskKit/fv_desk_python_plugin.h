// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_python_plugin.h — the host side of the `python` package kind
/// (port/extensions-plan.md §3).
///
/// Python lives in a separate library, libperegrine_python, linked against one
/// libpython and embedding pyfvw. It is opened at the first python package, so
/// Peregrine starts and runs without Python; when it will not open, python
/// packages are unavailable and nothing else changes. It is never closed, and
/// the interpreter is never finalised.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "fv_desk_tools.h"
#include "fvkit/overlay/overlay.h"

namespace fv {
namespace desk {

/// What the bridge library offers the host. Both are built together against
/// the shared core, so C++ types cross. Every call takes the GIL itself.
class PythonBridge {
 public:
  virtual ~PythonBridge() = default;
  /// The interpreter's "major.minor", e.g. "3.12".
  virtual std::string Version() const = 0;
  /// Puts `dir` at the front of sys.path, once.
  virtual Status AddPath(const std::string& dir) = 0;
  /// Imports `spec` ("module:Class") and answers a factory whose overlays are
  /// named `display_name`. kInvalidArg for a malformed spec; kNotFound when
  /// the class is missing or not a pyfvw.overlay.Overlay; kInternal with the
  /// Python error when the import raises.
  virtual Status ResolveOverlay(const std::string& spec, const std::string& display_name,
                                std::function<std::shared_ptr<Overlay>()>* out) = 0;
  /// Imports `spec` ("module:function") and answers a run that calls it with
  /// a pyfvw.app.ToolContext over the tool's context and `package_dir`.
  virtual Status ResolveTool(const std::string& spec, const std::string& package_dir,
                             ToolRun* out) = 0;
};

/// The bridge's C entry point, `fv_python_bridge_open`. Starts Python with
/// `python_home` (empty: the interpreter's own) and answers the bridge, or
/// null with `error` set.
using PythonBridgeOpen = PythonBridge* (*)(const char* python_home, std::string* error);

/// Where the bridge library is and the Python home it starts with. Takes
/// effect at the next load attempt; once a bridge has loaded it stays the
/// bridge. An empty path means `DefaultPythonBridgePath()`.
void SetPythonBridgeLocation(const std::string& path, const std::string& python_home = {});

/// libperegrine_python beside the binary that holds DeskKit (the shared core
/// in the apps), or empty when that is unknown.
std::string DefaultPythonBridgePath();

/// The process's bridge, loading it on first use. kNotFound when the bridge
/// file is absent; kIoError when it will not load (usually its libpython is
/// missing); kInternal when Python will not start. A failure is remembered
/// per path, so later calls answer at once.
Status LoadPythonBridge(PythonBridge** out);

/// The oldest `requires.python` a package may name.
constexpr const char* kMinimumPython = "3.10";

}  // namespace desk
}  // namespace fv
