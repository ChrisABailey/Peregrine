// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "curl_http.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>

namespace peregrine_web {
namespace {

/// curl_global_init is not thread safe; the first request does it once.
void GlobalInitOnce() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Transfer {
  fv::web::HttpResponse* out = nullptr;
  size_t max_body = 0;
  bool too_big = false;
  const std::atomic<bool>* cancel = nullptr;
};

size_t OnBody(char* data, size_t size, size_t count, void* user) {
  auto* t = static_cast<Transfer*>(user);
  const size_t n = size * count;
  if (t->out->body.size() + n > t->max_body) {
    t->too_big = true;
    return 0;
  }
  t->out->body.append(data, n);
  return n;
}

/// Keeps the headers of the last response; a redirect's are dropped.
size_t OnHeader(char* data, size_t size, size_t count, void* user) {
  auto* t = static_cast<Transfer*>(user);
  const size_t n = size * count;
  std::string line(data, n);
  if (line.rfind("HTTP/", 0) == 0) {
    t->out->headers.clear();
    return n;
  }
  const size_t colon = line.find(':');
  if (colon == std::string::npos) return n;
  std::string value = line.substr(colon + 1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(0, 1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  t->out->headers.emplace_back(line.substr(0, colon), value);
  return n;
}

int OnProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  auto* t = static_cast<Transfer*>(user);
  return t->cancel != nullptr && t->cancel->load() ? 1 : 0;
}

}  // namespace

CurlHttpClient::CurlHttpClient(std::string user_agent) : user_agent_(std::move(user_agent)) {}

fv::Status CurlHttpClient::Get(const fv::web::HttpRequest& request, const std::atomic<bool>* cancel,
                               fv::web::HttpResponse* out) {
  GlobalInitOnce();
  *out = fv::web::HttpResponse();
  if (cancel != nullptr && cancel->load()) return fv::Status::Error(fv::kInterrupted, "cancelled");
  CURL* curl = curl_easy_init();
  if (curl == nullptr) return fv::Status::Error(fv::kIoError, "libcurl did not start");

  Transfer t;
  t.out = out;
  t.max_body = request.max_body_bytes;
  t.cancel = cancel;
  curl_slist* list = nullptr;
  for (const auto& h : request.headers) list = curl_slist_append(list, (h.first + ": " + h.second).c_str());
  char error[CURL_ERROR_SIZE] = {0};
  const long timeout_ms = static_cast<long>(std::lround(request.timeout_s * 1000.0));

  curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent_.c_str());
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // every encoding libcurl decodes
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
  // Older distro libcurl (Ubuntu 22.04 ships 7.81) has only the bitmask form.
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
  curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, OnHeader);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &t);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout_ms, 15000L));
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(list);
  curl_easy_cleanup(curl);

  if (cancel != nullptr && cancel->load()) return fv::Status::Error(fv::kInterrupted, "cancelled");
  if (t.too_big)
    return fv::Status::Error(fv::kIoError, request.url + ": response larger than " +
                                               std::to_string(request.max_body_bytes) + " bytes");
  if (rc != CURLE_OK)
    return fv::Status::Error(fv::kIoError, request.url + ": " + (error[0] != '\0' ? error : curl_easy_strerror(rc)));
  out->status = static_cast<int>(status);
  return fv::Status::Ok();
}

}  // namespace peregrine_web
