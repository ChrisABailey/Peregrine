// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_sources.h — the layer manager's Sources view: the layers panel's
/// rows grouped by the file they come from (port/layer-manager-plan.md §1–§2),
/// and the read-only properties of a layer or a source.
///
/// A source is the path part of its overlays' file specs
/// (`fvkit/app/source_spec.h`); an overlay without a file is a source of its
/// own. Sources are listed where their topmost layer draws, each with its
/// layers in drawing order. Like `LayersModel`, rows are read from the stack on
/// every call; call only where the overlay stack may be changed.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "fv_desk_layers.h"
#include "fvkit/overlay/manager.h"

namespace fv {
namespace desk {

/// How many of a source's layers are shown.
enum class SourceShown { kNone, kSome, kAll };

/// One source, topmost first.
struct SourceRow {
  std::string path;       ///< the file; empty for an overlay without one
  std::string name;       ///< the file's name; a built-in overlay's type name; else its one layer's name
  std::string type_name;  ///< its first layer's type display name
  std::string icon;       ///< its first layer's type icon
  SourceShown shown = SourceShown::kAll;
  bool current = false;   ///< holds the current layer
  bool dirty = false;     ///< a layer has unsaved edits
  bool top_most = false;  ///< pinned above the rest
  /// The source's layers as indexes into `LayersModel::Rows()`, top first.
  std::vector<size_t> layers;
};

class SourcesModel {
 public:
  explicit SourcesModel(OverlayManager& stack) : stack_(stack), layers_(stack) {}

  std::vector<SourceRow> Rows() const;

  /// Shows or hides every layer of the source. kInvalidArg past the end.
  Status SetVisible(size_t source, bool visible);
  /// Moves source `from` to position `to` in the rows (0 = top), gathering its
  /// layers together directly above (moving up) or below (moving down) the
  /// layers of the source now at `to`. Moving across the top-most set is refused.
  Status Move(size_t from, size_t to);
  /// Makes the source's top layer current.
  Status Select(size_t source);

 private:
  OverlayManager& stack_;
  LayersModel layers_;
};

/// One line of a Properties sheet: a value under a section heading.
struct PropertyRow {
  std::string section;  ///< "Layer", "Source", "Layers"
  std::string key;
  std::string value;
};

/// What a layer is: its name, type, visibility and, for a feature layer, its
/// geometry, features, fields and extent; then its source.
std::vector<PropertyRow> LayerProperties(OverlayManager& stack, Overlay& overlay);
/// What a source is: its file (path, format, size, date) and its layers.
std::vector<PropertyRow> SourceProperties(OverlayManager& stack, const SourceRow& source);

/// The extent of the overlay's features; false for an overlay without
/// features or with none.
bool LayerExtent(const std::shared_ptr<Overlay>& overlay, GeoRect* out);

}  // namespace desk
}  // namespace fv
