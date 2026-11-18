// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "sse_stream.h"

#include <curl/curl.h>

#include "fvkit/ais/openwaters.h"

namespace peregrine_ais {

namespace {

/// curl_global_init is not thread safe; the first stream does it once.
void GlobalInitOnce() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Transfer {
  SseSplitter splitter;
  const SseStream::PayloadSink* sink = nullptr;
  std::atomic<bool>* stop = nullptr;
  std::atomic<StreamState>* state = nullptr;
};

size_t OnBody(char* data, size_t size, size_t count, void* user) {
  auto* t = static_cast<Transfer*>(user);
  const size_t n = size * count;
  if (t->stop->load()) return 0;
  t->state->store(StreamState::kLive);
  t->splitter.Feed(std::string_view(data, n), [t](std::string_view p) { (*t->sink)(p); });
  return n;
}

/// Aborts the transfer as soon as Close() asks, even while the stream is idle.
int OnProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
  return static_cast<Transfer*>(user)->stop->load() ? 1 : 0;
}

}  // namespace

void SseSplitter::Feed(std::string_view chunk,
                       const std::function<void(std::string_view)>& on_payload) {
  lines_.Append(chunk.data(), chunk.size());
  std::string line;
  while (lines_.NextLine(&line)) {
    const std::string_view payload = fv::ais::OpenWatersSsePayload(line);
    if (!payload.empty()) on_payload(payload);
  }
}

SseStream::SseStream(PayloadSink sink) : sink_(std::move(sink)) {}

SseStream::~SseStream() { Close(); }

void SseStream::Open(const std::string& url, std::vector<std::string> headers) {
  Close();
  GlobalInitOnce();
  stop_.store(false);
  http_status_.store(0);
  state_.store(StreamState::kConnecting);
  thread_ = std::thread(&SseStream::Run, this, url, std::move(headers));
}

void SseStream::Close() {
  stop_.store(true);
  if (thread_.joinable()) thread_.join();
  state_.store(StreamState::kOff);
}

std::string SseStream::last_error() const {
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

void SseStream::Run(std::string url, std::vector<std::string> headers) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = "libcurl did not start";
    state_.store(StreamState::kFailed);
    return;
  }
  Transfer t;
  t.sink = &sink_;
  t.stop = &stop_;
  t.state = &state_;
  curl_slist* list = nullptr;
  for (const std::string& h : headers) list = curl_slist_append(list, h.c_str());
  list = curl_slist_append(list, "Accept: text/event-stream");
  char error[CURL_ERROR_SIZE] = {0};

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
  // The server sends keep-alive comments; a minute of nothing is a dead stream.
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
  curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);

  const CURLcode rc = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  http_status_.store(status);
  curl_slist_free_all(list);
  curl_easy_cleanup(curl);

  if (stop_.load()) return;
  std::lock_guard<std::mutex> lock(error_mutex_);
  if (rc == CURLE_OK)
    last_error_ = "the server closed the stream";
  else
    last_error_ = error[0] != '\0' ? error : curl_easy_strerror(rc);
  state_.store(StreamState::kFailed);
}

}  // namespace peregrine_ais
