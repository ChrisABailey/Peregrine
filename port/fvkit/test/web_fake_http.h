// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// A fake `IHttpClient` for the web feature client tests.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "fvkit/feature/web/http.h"

namespace fv {
namespace web {

/// Answers from a table of URLs; a route ending in '*' matches any URL with
/// that prefix. Anything else is a 404. Records every URL asked for.
class FakeHttp : public IHttpClient {
 public:
  void Route(const std::string& url, std::string body, int status = 200) {
    routes_[url] = Answer{status, std::move(body)};
  }
  Status Get(const HttpRequest& request, const std::atomic<bool>*, HttpResponse* out) override {
    std::lock_guard<std::mutex> lock(mu_);
    requests_.push_back(request.url);
    auto it = routes_.find(request.url);
    if (it == routes_.end()) {
      for (auto r = routes_.begin(); r != routes_.end(); ++r) {
        const std::string& k = r->first;
        if (!k.empty() && k.back() == '*' && request.url.compare(0, k.size() - 1, k, 0, k.size() - 1) == 0) {
          it = r;
          break;
        }
      }
    }
    if (it == routes_.end()) {
      out->status = 404;
      out->body = "{}";
      return Status::Ok();
    }
    out->status = it->second.status;
    out->body = it->second.body;
    return Status::Ok();
  }
  std::vector<std::string> requests() {
    std::lock_guard<std::mutex> lock(mu_);
    return requests_;
  }

 private:
  struct Answer {
    int status;
    std::string body;
  };
  std::mutex mu_;
  std::map<std::string, Answer> routes_;
  std::vector<std::string> requests_;
};

}  // namespace web
}  // namespace fv
