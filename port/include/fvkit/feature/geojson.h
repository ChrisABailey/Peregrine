// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/geojson.h — GeoJSON (RFC 7946) to and from a feature layer
/// (port/feature-overlay-plan.md §1).
///
/// GeoJSON has no schema. The writer adds one as a top-level `fv_schema`
/// member, and the layer's style as `fv_style`, foreign members RFC 7946 §6.1
/// permits and other readers ignore; the reader
/// uses it when present and otherwise infers each property's type from the
/// values. Positions are [lon, lat] or [lon, lat, z]; M is not written.
#pragma once

#include <memory>
#include <string>

#include "fvkit/feature/layer.h"

namespace fv {

/// Reads a FeatureCollection, a single Feature or a bare geometry into a new
/// layer named `name` (the collection's own "name" member wins when present).
/// Integer feature ids are kept when every feature has a distinct positive
/// one; otherwise features are numbered from 1 in file order. kInvalidArg,
/// naming the feature, for malformed JSON or geometry. `style_json`, when
/// set, receives the top-level `fv_style` member as text, or "" without one.
Status ReadGeoJson(const std::string& text, const std::string& name,
                   std::shared_ptr<MemoryFeatureLayer>* out, std::string* style_json = nullptr);
/// `ReadGeoJson` on a file; the layer is named after the file's stem.
Status ReadGeoJsonFile(const std::string& path, std::shared_ptr<MemoryFeatureLayer>* out,
                       std::string* style_json = nullptr);

/// Writes `layer` as a FeatureCollection with its schema. Exterior rings are
/// written counter-clockwise and holes clockwise (RFC 7946 §3.1.6).
/// kUnsupported for a layer with a blob field. A non-empty `style_json`
/// (`WriteFeatureStyleJson`) is embedded as the top-level `fv_style` member;
/// kInvalidArg when it is not a JSON object.
Status WriteGeoJson(const IFeatureLayer& layer, std::string* out, const std::string& style_json = "");
Status WriteGeoJsonFile(const IFeatureLayer& layer, const std::string& path,
                        const std::string& style_json = "");

}  // namespace fv
