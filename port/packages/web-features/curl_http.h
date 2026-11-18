// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// curl_http.h — the libcurl `IHttpClient` web feature layers fetch through
/// (port/web-features-plan.md WF-D1, WF-D10).
#pragma once

#include <atomic>
#include <string>

#include "fvkit/feature/web/http.h"

namespace peregrine_web {

/// A blocking GET per call on its own easy handle, so calls from several
/// threads do not share state. Follows up to five redirects, accepts gzip
/// and deflate, and sends `user_agent` on every request.
class CurlHttpClient : public fv::web::IHttpClient {
 public:
  explicit CurlHttpClient(std::string user_agent);

  fv::Status Get(const fv::web::HttpRequest& request, const std::atomic<bool>* cancel,
                 fv::web::HttpResponse* out) override;

 private:
  std::string user_agent_;
};

}  // namespace peregrine_web
