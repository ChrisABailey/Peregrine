// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_overlay_manifest.h — overlay types declared as JSON
/// (port/desktop-plan.md §3a).
///
/// A manifest entry fills an `app::OverlayTypeDesc` with everything but the
/// factories; its `implementation` names where those come from. Only the
/// `builtin` kind exists: C++ code in the process registers a factory under a
/// name with `RegisterBuiltinOverlay` (or a `BuiltinOverlayRegistrar` at file
/// scope), and the manifest refers to it by that name. `library` and `python`
/// entries parse but are reported as unsupported when registered.
///
/// A manifest file holds either one entry object or `{"overlays": [ ... ]}`:
///
///     { "id": "user.rangerings", "display_name": "Range Rings",
///       "icon": "rangerings.svg", "kind": "file", "display_order": 40,
///       "file": { "extension": "rng", "filters": [["Range Rings", "*.rng"]] },
///       "implementation": { "builtin": "user.rangerings" } }
///
/// Optional keys: `parent_display_name`, `top_most`, `opacity` (0–100),
/// `user_controllable`, `restore_at_startup`, and under `file`:
/// `directory`, `save_filters` (defaults to `filters`). `kind` may be
/// omitted when the presence of `file` says it.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/app/editor.h"
#include "fvkit/app/type_registry.h"

namespace fv {
namespace desk {

/// The code half of a `builtin` overlay type.
struct BuiltinOverlay {
  std::function<std::shared_ptr<Overlay>()> factory;  ///< required
  std::function<std::unique_ptr<app::OverlayEditor>()> editor_factory;  ///< may be null
};

/// Adds `impl` to the process-wide builtin table under `name`. kInvalidArg for
/// an empty name, a null factory or a name already taken.
Status RegisterBuiltinOverlay(const std::string& name, BuiltinOverlay impl);
/// The implementation registered under `name`, or null.
const BuiltinOverlay* FindBuiltinOverlay(const std::string& name);
/// Removes `name` from the table; for tests. False when absent.
bool UnregisterBuiltinOverlay(const std::string& name);

/// Registers a builtin during static initialisation. In a static library the
/// object file holding one is linked only if something else in it is
/// referenced, so a builtin there is registered from code the app calls.
struct BuiltinOverlayRegistrar {
  BuiltinOverlayRegistrar(const std::string& name, BuiltinOverlay impl);
};

/// One parsed manifest entry.
struct OverlayManifestEntry {
  app::OverlayTypeDesc desc;        ///< factories empty
  std::string implementation_kind;  ///< "builtin", "library" or "python"
  std::string implementation_name;  ///< the value under that key
  std::string source;               ///< the file (or name) it was read from
};

/// Parses one manifest document. All or nothing: on error `out` is unchanged.
/// An icon ending in .svg, .png or .pdf is a file and is resolved against
/// `base_dir`; any other icon is a symbolic name and kept as written.
Status ParseOverlayManifest(const std::string& text, const std::string& source,
                            const std::string& base_dir,
                            std::vector<OverlayManifestEntry>* out);
/// Reads and parses one manifest file; `base_dir` is the file's directory.
Status ReadOverlayManifestFile(const std::string& path, std::vector<OverlayManifestEntry>* out);

/// Resolves the entry's implementation and registers the type.
/// kUnsupported for a non-builtin kind, kNotFound for an unknown builtin name;
/// otherwise whatever `OverlayTypeRegistry::Register` answers.
Status RegisterManifestEntry(app::OverlayTypeRegistry& registry, const OverlayManifestEntry& entry);

/// Reads every `*.json` file in each directory (directories in order, files by
/// name) and registers its entries. A missing directory is skipped silently;
/// a file that will not parse and an entry that will not register are named
/// in `warnings` and the rest still load. Returns the ids registered.
std::vector<app::TypeId> RegisterOverlayManifests(app::OverlayTypeRegistry& registry,
                                                  const std::vector<std::string>& dirs,
                                                  std::vector<std::string>* warnings);

/// The per-user manifest directory: `overlays/` beside the user settings
/// file. Empty when no home directory is known. The app bundle's own
/// directory is the shell's to add.
std::string DefaultUserOverlayManifestDir();

}  // namespace desk
}  // namespace fv
