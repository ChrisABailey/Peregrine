// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/ogc_api.h — an OGC API – Features (Part 1, Core) client
/// over GeoJSON (port/web-features-plan.md WF2).
///
/// Collections whose `itemType` is "feature" (or absent) are offered as
/// layers. A layer's fields come from the collection's schema or queryables
/// document when the server has one, else from a sample of its items. Pages
/// follow the server's `next` links. Coordinates are CRS84, the default.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/feature/web/http.h"
#include "fvkit/feature/web/service.h"

namespace fv {
namespace web {

struct OgcApiOptions {
  int limit = 1000;        ///< features asked for per page
  int sample_limit = 50;   ///< items read to infer fields when no schema is published
};

/// A feature id for an OGC id given as text: the number itself when the text
/// is a positive decimal integer that fits, else a non-zero 63-bit hash.
int64_t OgcFeatureId(const std::string& text);

class OgcApiFeatures : public IFeatureService {
 public:
  /// Reads the landing page (or a collections document) at `url`.
  /// kUnsupported when it is neither; kIoError for a transfer failure.
  static Status Open(std::shared_ptr<IHttpClient> http, const std::string& url,
                     OgcApiOptions options, std::shared_ptr<OgcApiFeatures>* out,
                     const std::atomic<bool>* cancel = nullptr);
  ~OgcApiFeatures() override;

  Status ListLayers(std::vector<RemoteLayerInfo>* out, const std::atomic<bool>* cancel) override;
  Status Describe(const std::string& layer_id, RemoteLayerInfo* out,
                  const std::atomic<bool>* cancel) override;
  Status FetchPage(const std::string& layer_id, const GeoRect& box, const std::string& cursor,
                   std::vector<Feature>* out, std::string* next,
                   const std::atomic<bool>* cancel) override;

  /// The landing page's title; empty when it has none.
  const std::string& title() const;

  struct Impl;

 private:
  explicit OgcApiFeatures(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace web
}  // namespace fv
