// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_web_layers.h — the Add Web Layer dialog's model: a service URL,
/// the layers it offers (listed on a worker thread), the recent URLs, and
/// the status-line text for web layers loading (port/web-features-plan.md WF6).
///
/// Listing goes through the feature-source registry, so DeskKit needs no
/// network code: the web package's source answers for http(s) URLs.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fvkit/feature/feature_source.h"

namespace fv {
class OverlayManager;

namespace desk {
class UserSettings;

/// Where the dialog's connection stands.
enum class WebConnectState {
  kIdle = 0,        ///< nothing asked yet
  kConnecting = 1,  ///< listing on the worker
  kConnected = 2,   ///< `rows()` holds the service's layers
  kFailed = 3,      ///< `error()` says why
};

/// One layer the service offers, as the dialog lists it.
struct WebLayerRow {
  std::string spec;      ///< opens the layer
  std::string id;
  std::string title;
  std::string geometry;  ///< "Point", "Area"…; empty when the service does not say
  std::string description;
  bool chosen = false;   ///< ticked for Add
};

/// The URL as typed, cleaned: surrounding blanks dropped, `https://` added
/// when no scheme is given. Empty for blank text.
std::string NormalizeWebUrl(const std::string& text);

/// Lists one service's layers at a time, off the calling thread. UI thread
/// only, apart from the worker it owns.
class WebLayerConnection {
 public:
  WebLayerConnection();
  /// Cancels any listing in progress and waits for its worker.
  ~WebLayerConnection();
  WebLayerConnection(const WebLayerConnection&) = delete;
  WebLayerConnection& operator=(const WebLayerConnection&) = delete;

  /// Starts listing the layers of the service at `url` (`NormalizeWebUrl`
  /// applied); a listing in progress is cancelled. kInvalidArg for blank
  /// text or a URL no registered source claims.
  Status Connect(const std::string& url);
  /// Cancels a listing in progress; the state goes back to kIdle.
  void Cancel();
  /// Takes a finished listing's result. True once per finished `Connect`.
  bool Poll();

  WebConnectState state() const { return state_; }
  const std::string& url() const { return url_; }
  const std::string& error() const { return error_; }
  const std::vector<WebLayerRow>& rows() const { return rows_; }
  /// Ticks or unticks row `i`; nothing for a row out of range.
  void SetChosen(size_t i, bool chosen);
  /// The specs of the ticked rows, in list order.
  std::vector<std::string> ChosenSpecs() const;
  /// "Connecting to <url>…", "3 layers", or the error.
  std::string StatusLine() const;

 private:
  struct Attempt;
  void Retire();

  WebConnectState state_ = WebConnectState::kIdle;
  std::string url_;
  std::string error_;
  std::vector<WebLayerRow> rows_;
  std::shared_ptr<Attempt> attempt_;
  std::thread worker_;
  std::vector<std::thread> retired_;  ///< cancelled workers, joined on destruction
};

/// Most URLs the dialog remembers.
constexpr size_t kMaxRecentWebUrls = 8;
/// The remembered service URLs, most recent first (user settings `web.recent.<n>`).
std::vector<std::string> RecentWebUrls(const UserSettings& user);
/// Moves `url` to the top of the remembered list, dropping the oldest past the limit.
void NoteRecentWebUrl(UserSettings* user, const std::string& url);

/// The status-line text for the web layers in `overlays`: tiles loading, a
/// layer that stopped at its feature cap, or the latest failed fetch. Empty
/// when there is nothing to say.
std::string WebLayersStatus(const OverlayManager& overlays);

}  // namespace desk
}  // namespace fv
