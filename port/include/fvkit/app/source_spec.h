// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/app/source_spec.h — file specs that name one layer of a file.
///
/// A file spec names a whole file ("a.gpkg") or one layer of a file that holds
/// several ("a.gpkg|layername=rivers", the form QGIS writes). The part before
/// the separator is the overlay's source: overlays whose specs share it are
/// layers of one source (port/layer-manager-plan.md §1).
#pragma once

#include <string>

namespace fv {
namespace app {

/// What separates a file's path from the layer it names.
extern const char kLayerSpecSeparator[];  // "|layername="

/// A file spec split into the file and the layer within it.
struct SourceSpec {
  std::string path;   ///< the file; the whole spec when it names no layer
  std::string layer;  ///< empty for a spec naming the whole file
};

/// Splits `spec` at the last separator; a spec without one is all path.
SourceSpec SplitSourceSpec(const std::string& spec);

/// The spec of layer `layer` of `path`; `path` alone when `layer` is empty.
std::string JoinSourceSpec(const std::string& path, const std::string& layer);

}  // namespace app
}  // namespace fv
