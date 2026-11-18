// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/web_source.h — the feature source for web services:
/// an http(s) spec opens an OGC API – Features collection or an Esri layer
/// as a cached `RemoteFeatureLayer` (port/web-features-plan.md WF-D3, WF-D4).
///
/// The package supplying the HTTP transport registers the source this makes.
/// The service kind follows from the URL: a path ending in /FeatureServer or
/// /MapServer is Esri, anything else OGC API. An Esri layer comes with its
/// service style (WF-D11).
#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "fvkit/feature/feature_source.h"
#include "fvkit/feature/web/http.h"
#include "fvkit/feature/web/remote_layer.h"
#include "fvkit/feature/web/service.h"

namespace fv {
namespace web {

enum class WebServiceKind { kOgcApi, kEsri };

/// True for a spec whose source is an http:// or https:// URL.
bool IsWebFeatureSpec(const std::string& spec);
/// The kind of service at `url`, from its path alone.
WebServiceKind WebServiceKindOf(const std::string& url);

/// Opens the service at `url` (no layer part) as its kind says.
Status OpenWebService(std::shared_ptr<IHttpClient> http, const std::string& url,
                      std::shared_ptr<IFeatureService>* out, const std::atomic<bool>* cancel = nullptr);

struct WebSourceOptions {
  std::string cache_dir;  ///< caches go under `<cache_dir>/web-features`
  RemoteLayerOptions layer;
};

/// The "peregrine.web" source over `http`. A spec without a layer part opens
/// the service's first layer. Layers of one service URL share one service
/// object, so it is read once per process.
FeatureSourceDesc MakeWebFeatureSource(std::shared_ptr<IHttpClient> http, WebSourceOptions options);

}  // namespace web
}  // namespace fv
