// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// sse_stream.h — a server-sent-events reader over libcurl for the Open Waters
/// AIS stream. One connection at a time on a thread of its own; every `data:`
/// payload is handed to a callback on that thread.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fvkit/nav/line_transport.h"

namespace peregrine_ais {

/// Splits an SSE byte stream into `data:` payloads. Lines may arrive split
/// across chunks; CR LF and LF both end a line.
class SseSplitter {
 public:
  /// Appends `chunk` and calls `on_payload` for each complete `data:` line.
  void Feed(std::string_view chunk, const std::function<void(std::string_view)>& on_payload);

 private:
  /// Large enough for a snapshot frame with several multipart sentences.
  fv::LineBuffer lines_{1 << 16};
};

enum class StreamState { kOff, kConnecting, kLive, kFailed };

/// One HTTPS GET held open, its body read as SSE.
class SseStream {
 public:
  using PayloadSink = std::function<void(std::string_view payload)>;

  /// `sink` runs on the stream's thread and must not call back into the stream.
  explicit SseStream(PayloadSink sink);
  ~SseStream();
  SseStream(const SseStream&) = delete;
  SseStream& operator=(const SseStream&) = delete;

  /// Closes any open connection and starts a new one. `headers` are
  /// complete "Name: value" lines.
  void Open(const std::string& url, std::vector<std::string> headers);
  /// Closes the connection and waits for its thread.
  void Close();

  StreamState state() const { return state_.load(); }
  /// Why the last connection ended; empty while none has.
  std::string last_error() const;
  /// The HTTP status of the last response, 0 before one.
  long http_status() const { return http_status_.load(); }

 private:
  void Run(std::string url, std::vector<std::string> headers);

  PayloadSink sink_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  std::atomic<StreamState> state_{StreamState::kOff};
  std::atomic<long> http_status_{0};
  mutable std::mutex error_mutex_;
  std::string last_error_;
};

}  // namespace peregrine_ais
