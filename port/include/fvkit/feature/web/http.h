// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fvkit/feature/web/http.h — the HTTP transport web feature layers fetch
/// through (port/web-features-plan.md WF-D2).
///
/// fvkit links no network library. A package supplies the implementation
/// (`peregrine.web`, libcurl); tests supply a fake over recorded responses.
#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "fvkit/geo.h"

namespace fv {
namespace web {

/// Largest response body a client accepts (WF-D10).
constexpr size_t kMaxResponseBytes = 64u * 1024u * 1024u;

struct HttpRequest {
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  double timeout_s = 30.0;
  size_t max_body_bytes = kMaxResponseBytes;
};

struct HttpResponse {
  int status = 0;  ///< the HTTP status code
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;  ///< decompressed
};

/// A blocking HTTP GET. Implementations are safe to call from several threads.
class IHttpClient {
 public:
  virtual ~IHttpClient() = default;
  /// Fetches `request.url`. Any HTTP status is a successful transfer (the
  /// caller reads `status`); kIoError for a connection failure, a timeout or a
  /// body over `max_body_bytes`; kInterrupted once `*cancel` is set.
  virtual Status Get(const HttpRequest& request, const std::atomic<bool>* cancel,
                     HttpResponse* out) = 0;
};

}  // namespace web
}  // namespace fv
