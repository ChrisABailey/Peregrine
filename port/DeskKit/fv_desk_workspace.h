// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_workspace.h — the saved state of one map window.
///
/// A workspace is the catalog, the map group and the product drawn, the
/// camera, the overlay stack and the active editor, written as a JSON file
/// (`.fvws`). The overlay stack is the `OverlaySession` configuration,
/// stored key for key so that restoring goes through the session's own
/// flows (dedup, unknown-type warnings, order).
#pragma once

#include <map>
#include <string>

#include "fvkit/geo.h"
#include "fvkit/proj.h"

namespace fv {
namespace desk {

struct Workspace {
  std::string catalog_path;    ///< empty: no catalog
  std::string map_group;       ///< MapGroup id; empty: none chosen
  std::string product_format;  ///< the series drawn when saved; empty: none
  std::string product_series_key;
  GeoPoint center;
  double scale_denom = 1.0e6;
  double rotation_deg = 0.0;
  ProjectionType projection = ProjectionType::kEqualArc;
  /// `OverlaySession` configuration keys with the `session.<name>.` prefix
  /// removed: "count", "current", "declutter", "0.type", "0.file", …
  std::map<std::string, std::string> overlays;
  std::string active_editor;  ///< TypeId of the editor in use; empty: none

  /// Pretty-printed JSON.
  std::string ToJson() const;
  /// All or nothing: on error `*this` is unchanged.
  Status FromJson(const std::string& text, const std::string& name = "<string>");
  Status Save(const std::string& path) const;
  Status Load(const std::string& path);

  bool operator==(const Workspace& o) const;
  bool operator!=(const Workspace& o) const { return !(*this == o); }
};

/// The workspace file extension, without a dot.
extern const char kWorkspaceExtension[];  // "fvws"

/// Stable key for a projection type ("equal_arc", "mercator", "lambert",
/// "azimuthal_equidistant", "orthographic"), used in files and command ids.
const char* ProjectionKey(ProjectionType t);
/// The menu title ("Equal Arc", …).
const char* ProjectionTitle(ProjectionType t);
/// False for an unknown key.
bool ParseProjectionKey(const std::string& key, ProjectionType* out);
/// Every projection type, in menu order.
const ProjectionType* AllProjectionTypes(size_t* count);

}  // namespace desk
}  // namespace fv
