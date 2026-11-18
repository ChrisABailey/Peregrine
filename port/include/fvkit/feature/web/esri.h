// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/esri.h — an Esri GeoServices REST client for the layers
/// of a FeatureServer or MapServer (port/web-features-plan.md WF3).
///
/// Features are asked for in WGS 84 (`inSR`/`outSR` 4326) as GeoJSON, or as
/// Esri JSON when the server does not offer GeoJSON. A layer that supports
/// pagination is paged by `resultOffset`; one that does not is read by asking
/// for the object ids in the box, then the features in blocks of ids.
/// Line and area geometries always come back as MultiLineString and
/// MultiPolygon, so every feature matches the layer's geometry type.
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

struct EsriOptions {
  /// Features asked for per page; 0 or more than the layer's `maxRecordCount`
  /// means `maxRecordCount`.
  int page_size = 0;
};

/// An epoch-milliseconds Esri date as ISO 8601 UTC ("2026-11-17T17:14:00Z";
/// fractional seconds only when the milliseconds are not zero).
std::string EsriDateText(int64_t epoch_ms);

class EsriFeatureService : public IFeatureService {
 public:
  /// Reads the service document at `url` (a .../FeatureServer or .../MapServer
  /// URL; a query string or trailing slash is dropped). kUnsupported when it is
  /// not one (an Esri layer URL included), or the service needs a sign-in;
  /// kIoError for a transfer failure or an Esri error response.
  static Status Open(std::shared_ptr<IHttpClient> http, const std::string& url, EsriOptions options,
                     std::shared_ptr<EsriFeatureService>* out,
                     const std::atomic<bool>* cancel = nullptr);
  ~EsriFeatureService() override;

  /// The service's feature layers; group layers and tables are left out. The
  /// list carries ids, titles and geometry types; `Describe` adds fields and extent.
  Status ListLayers(std::vector<RemoteLayerInfo>* out, const std::atomic<bool>* cancel) override;
  Status Describe(const std::string& layer_id, RemoteLayerInfo* out,
                  const std::atomic<bool>* cancel) override;
  Status FetchPage(const std::string& layer_id, const GeoRect& box, const std::string& cursor,
                   std::vector<Feature>* out, std::string* next,
                   const std::atomic<bool>* cancel) override;

  /// The layer's `drawingInfo` object as JSON text (renderer, transparency,
  /// label classes), for `EsriStyleFromDrawingInfo`; empty when it has none.
  Status DrawingInfo(const std::string& layer_id, std::string* out,
                     const std::atomic<bool>* cancel = nullptr);

  /// The service's title (`documentInfo.Title`, else the service name).
  const std::string& title() const;

  struct Impl;

 private:
  explicit EsriFeatureService(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace web
}  // namespace fv
