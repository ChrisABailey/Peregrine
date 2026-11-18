// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/service.h — one web feature service as the remote layer
/// sees it: its layers, their schemas, and a page of features in a box.
///
/// The OGC API – Features and Esri FeatureServer clients implement this
/// (port/web-features-plan.md WF2, WF3); `RemoteFeatureLayer` drives it.
#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "fvkit/feature/layer.h"

namespace fv {
namespace web {

/// One layer (OGC collection, Esri layer) a service offers.
struct RemoteLayerInfo {
  std::string id;     ///< the collection id or Esri layer number, as text
  std::string title;  ///< display name; `id` when the service gives none
  std::string description;
  FeatureSchema schema;
  GeoRect extent;  ///< WGS 84; meaningful only when `has_extent`
  bool has_extent = false;
};

/// A web feature service. Calls block and may run on any thread.
class IFeatureService {
 public:
  virtual ~IFeatureService() = default;
  /// The feature layers the service offers.
  virtual Status ListLayers(std::vector<RemoteLayerInfo>* out, const std::atomic<bool>* cancel) = 0;
  /// One layer's description. kNotFound for an id the service lacks.
  virtual Status Describe(const std::string& layer_id, RemoteLayerInfo* out,
                          const std::atomic<bool>* cancel) = 0;
  /// One page of the features of `layer_id` intersecting `box` (never crossing
  /// ±180). `cursor` is empty for the first page and the previous call's
  /// `*next` after it; `*next` comes back empty after the last page. Features
  /// carry the service's id as a non-zero `id` and values parallel to
  /// `Describe`'s schema.
  virtual Status FetchPage(const std::string& layer_id, const GeoRect& box,
                           const std::string& cursor, std::vector<Feature>* out,
                           std::string* next, const std::atomic<bool>* cancel) = 0;
};

}  // namespace web
}  // namespace fv
