// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The web feature source: URL specs to cached remote layers.

#include "fvkit/feature/web/web_source.h"

#include <cctype>
#include <map>
#include <mutex>

#include "fvkit/app/source_spec.h"
#include "fvkit/feature/style.h"
#include "fvkit/feature/web/esri.h"
#include "fvkit/feature/web/esri_style.h"
#include "fvkit/feature/web/ogc_api.h"

namespace fv {
namespace web {
namespace {

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// `url` without its query string and trailing slashes.
std::string BaseUrl(const std::string& url) {
  std::string base = url.substr(0, url.find('?'));
  while (!base.empty() && base.back() == '/') base.pop_back();
  return base;
}

}  // namespace

bool IsWebFeatureSpec(const std::string& spec) {
  const std::string head = Lower(spec.substr(0, 8));
  return head.rfind("http://", 0) == 0 || head.rfind("https://", 0) == 0;
}

WebServiceKind WebServiceKindOf(const std::string& url) {
  const std::string path = Lower(BaseUrl(url));
  for (const char* tail : {"/featureserver", "/mapserver"})
    if (path.size() >= std::char_traits<char>::length(tail) &&
        path.compare(path.size() - std::char_traits<char>::length(tail), std::string::npos, tail) == 0)
      return WebServiceKind::kEsri;
  return WebServiceKind::kOgcApi;
}

Status OpenWebService(std::shared_ptr<IHttpClient> http, const std::string& url,
                      std::shared_ptr<IFeatureService>* out, const std::atomic<bool>* cancel) {
  if (WebServiceKindOf(url) == WebServiceKind::kEsri) {
    std::shared_ptr<EsriFeatureService> esri;
    Status s = EsriFeatureService::Open(std::move(http), url, EsriOptions(), &esri, cancel);
    if (s.ok()) *out = esri;
    return s;
  }
  std::shared_ptr<OgcApiFeatures> ogc;
  Status s = OgcApiFeatures::Open(std::move(http), url, OgcApiOptions(), &ogc, cancel);
  if (s.ok()) *out = ogc;
  return s;
}

FeatureSourceDesc MakeWebFeatureSource(std::shared_ptr<IHttpClient> http, WebSourceOptions options) {
  struct Shared {
    std::shared_ptr<IHttpClient> http;
    WebSourceOptions options;
    std::mutex mu;
    std::map<std::string, std::shared_ptr<IFeatureService>> services;  ///< by base URL
  };
  auto shared = std::make_shared<Shared>();
  shared->http = std::move(http);
  shared->options = std::move(options);

  FeatureSourceDesc d;
  d.name = "peregrine.web";
  d.matches = [](const std::string& spec) { return IsWebFeatureSpec(spec); };
  // The service object for `path`, opened once per base URL. Opened outside
  // the lock: a slow server must not hold up other sources.
  auto service_for = [shared](const std::string& path, std::shared_ptr<IFeatureService>* service,
                              const std::atomic<bool>* cancel) -> Status {
    const std::string url = BaseUrl(path);
    {
      std::lock_guard<std::mutex> lock(shared->mu);
      auto it = shared->services.find(url);
      if (it != shared->services.end()) {
        *service = it->second;
        return Status::Ok();
      }
    }
    std::shared_ptr<IFeatureService> opened;
    Status s = OpenWebService(shared->http, path, &opened, cancel);
    if (!s.ok()) return s;
    std::lock_guard<std::mutex> lock(shared->mu);
    *service = shared->services.emplace(url, std::move(opened)).first->second;
    return Status::Ok();
  };
  d.list = [service_for](const std::string& spec, std::vector<FeatureSourceLayer>* out,
                         const std::atomic<bool>* cancel) -> Status {
    const std::string path = app::SplitSourceSpec(spec).path;
    std::shared_ptr<IFeatureService> service;
    Status s = service_for(path, &service, cancel);
    if (!s.ok()) return s;
    std::vector<RemoteLayerInfo> layers;
    s = service->ListLayers(&layers, cancel);
    if (!s.ok()) return s;
    const std::string url = BaseUrl(path);
    for (const RemoteLayerInfo& info : layers) {
      FeatureSourceLayer l;
      l.spec = app::JoinSourceSpec(url, info.id);
      l.id = info.id;
      l.title = info.title;
      l.description = info.description;
      l.geometry_type = info.schema.geometry_type;
      out->push_back(std::move(l));
    }
    return Status::Ok();
  };
  d.open = [shared, service_for](const std::string& spec, FeatureSourceResult* out) -> Status {
    const app::SourceSpec source = app::SplitSourceSpec(spec);
    const std::string url = BaseUrl(source.path);
    std::shared_ptr<IFeatureService> service;
    Status opened = service_for(source.path, &service, nullptr);
    if (!opened.ok()) return opened;

    std::string layer_id = source.layer;
    if (layer_id.empty()) {
      std::vector<RemoteLayerInfo> layers;
      Status s = service->ListLayers(&layers, nullptr);
      if (!s.ok()) return s;
      if (layers.empty()) return Status::Error(kNotFound, url + " offers no feature layers");
      layer_id = layers.front().id;
    }
    RemoteLayerInfo info;
    Status s = service->Describe(layer_id, &info, nullptr);
    if (!s.ok()) return s;
    std::shared_ptr<RemoteFeatureLayer> layer;
    s = RemoteFeatureLayer::Open(service, info, WebCachePath(shared->options.cache_dir, url),
                                 shared->options.layer, &layer);
    if (!s.ok()) return s;
    out->layer = layer;

    if (auto* esri = dynamic_cast<EsriFeatureService*>(service.get())) {
      std::string drawing_info;
      if (esri->DrawingInfo(layer_id, &drawing_info).ok() && !drawing_info.empty()) {
        FeatureStyle style;
        if (EsriStyleFromDrawingInfo(drawing_info, info.schema, &style, &out->notes).ok())
          WriteFeatureStyleJson(style, &out->style_json);
      }
    }
    return Status::Ok();
  };
  return d;
}

}  // namespace web
}  // namespace fv
