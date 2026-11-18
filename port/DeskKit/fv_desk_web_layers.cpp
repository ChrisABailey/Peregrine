// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// The Add Web Layer dialog's connection, recent URLs and status text.

#include "fv_desk_web_layers.h"

#include <algorithm>
#include <cctype>
#include <mutex>

#include "fv_desk_user_settings.h"
#include "fvkit/feature/geometry.h"
#include "fvkit/feature/web/remote_layer.h"
#include "fvkit/overlay/feature_overlay.h"
#include "fvkit/overlay/manager.h"

namespace fv {
namespace desk {
namespace {

constexpr char kRecentPrefix[] = "web.recent.";

/// "200,000".
std::string Thousands(size_t n) {
  std::string digits = std::to_string(n);
  for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) digits.insert(static_cast<size_t>(at), ",");
  return digits;
}

}  // namespace

/// One `Connect`'s worker state, shared with its thread.
struct WebLayerConnection::Attempt {
  std::atomic<bool> cancel{false};
  std::mutex mu;
  bool done = false;
  Status status;
  std::vector<FeatureSourceLayer> layers;
};

std::string NormalizeWebUrl(const std::string& text) {
  size_t b = 0;
  size_t e = text.size();
  while (b < e && std::isspace(static_cast<unsigned char>(text[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1]))) --e;
  std::string url = text.substr(b, e - b);
  if (url.empty()) return url;
  if (url.find("://") == std::string::npos) url = "https://" + url;
  return url;
}

WebLayerConnection::WebLayerConnection() = default;

WebLayerConnection::~WebLayerConnection() {
  Retire();
  for (std::thread& t : retired_)
    if (t.joinable()) t.join();
}

void WebLayerConnection::Retire() {
  if (attempt_) attempt_->cancel = true;
  attempt_.reset();
  // Joining here would hold up the UI on a slow server; the worker exits on its own.
  if (worker_.joinable()) retired_.push_back(std::move(worker_));
}

Status WebLayerConnection::Connect(const std::string& text) {
  const std::string url = NormalizeWebUrl(text);
  if (url.empty()) return Status::Error(kInvalidArg, "enter the address of a web feature service");
  if (!IsFeatureSourceSpec(url))
    return Status::Error(kInvalidArg, "no installed package opens " + url +
                                          "; web layers need an http:// or https:// address");
  Retire();
  url_ = url;
  error_.clear();
  rows_.clear();
  state_ = WebConnectState::kConnecting;
  auto attempt = std::make_shared<Attempt>();
  attempt_ = attempt;
  worker_ = std::thread([attempt, url] {
    std::vector<FeatureSourceLayer> layers;
    const Status s = ListFeatureSourceLayers(url, &layers, &attempt->cancel);
    std::lock_guard<std::mutex> lock(attempt->mu);
    attempt->status = s;
    attempt->layers = std::move(layers);
    attempt->done = true;
  });
  return Status::Ok();
}

void WebLayerConnection::Cancel() {
  if (state_ != WebConnectState::kConnecting) return;
  Retire();
  state_ = WebConnectState::kIdle;
}

bool WebLayerConnection::Poll() {
  if (!attempt_) return false;
  {
    std::lock_guard<std::mutex> lock(attempt_->mu);
    if (!attempt_->done) return false;
    if (!attempt_->status.ok()) {
      error_ = attempt_->status.message;
      state_ = WebConnectState::kFailed;
    } else {
      for (const FeatureSourceLayer& l : attempt_->layers) {
        WebLayerRow row;
        row.spec = l.spec;
        row.id = l.id;
        row.title = l.title.empty() ? l.id : l.title;
        if (l.geometry_type != GeometryType::kGeometry) row.geometry = GeometryTypeTitle(l.geometry_type);
        row.description = l.description;
        rows_.push_back(std::move(row));
      }
      // A service with one layer has nothing to choose between.
      if (rows_.size() == 1) rows_.front().chosen = true;
      state_ = WebConnectState::kConnected;
    }
  }
  attempt_.reset();
  if (worker_.joinable()) worker_.join();
  return true;
}

void WebLayerConnection::SetChosen(size_t i, bool chosen) {
  if (i < rows_.size()) rows_[i].chosen = chosen;
}

std::vector<std::string> WebLayerConnection::ChosenSpecs() const {
  std::vector<std::string> out;
  for (const WebLayerRow& r : rows_)
    if (r.chosen) out.push_back(r.spec);
  return out;
}

std::string WebLayerConnection::StatusLine() const {
  switch (state_) {
    case WebConnectState::kIdle: return std::string();
    case WebConnectState::kConnecting: return "Connecting to " + url_ + "\xE2\x80\xA6";
    case WebConnectState::kFailed: return error_;
    case WebConnectState::kConnected:
      if (rows_.empty()) return "The service offers no feature layers";
      return rows_.size() == 1 ? std::string("1 layer") : std::to_string(rows_.size()) + " layers";
  }
  return std::string();
}

std::vector<std::string> RecentWebUrls(const UserSettings& user) {
  std::vector<std::string> out;
  for (size_t i = 0; i < kMaxRecentWebUrls; ++i) {
    const std::string url = user.Get(kRecentPrefix + std::to_string(i));
    if (url.empty()) break;
    out.push_back(url);
  }
  return out;
}

void NoteRecentWebUrl(UserSettings* user, const std::string& url) {
  if (user == nullptr || url.empty()) return;
  std::vector<std::string> urls = RecentWebUrls(*user);
  urls.erase(std::remove(urls.begin(), urls.end(), url), urls.end());
  urls.insert(urls.begin(), url);
  if (urls.size() > kMaxRecentWebUrls) urls.resize(kMaxRecentWebUrls);
  user->ErasePrefix(kRecentPrefix);
  for (size_t i = 0; i < urls.size(); ++i) user->Set(kRecentPrefix + std::to_string(i), urls[i]);
}

std::string WebLayersStatus(const OverlayManager& overlays) {
  size_t tiles = 0;
  std::string capped;
  std::string failed;
  for (const std::shared_ptr<Overlay>& o : overlays.Overlays()) {
    const auto* f = dynamic_cast<const FeatureOverlay*>(o.get());
    const web::RemoteFeatureLayer* remote = f != nullptr ? f->remote_layer() : nullptr;
    if (remote == nullptr) continue;
    const web::RemoteLayerProgress p = remote->Progress();
    tiles += p.tiles_queued + p.tiles_loading;
    if (p.capped && capped.empty())
      capped = f->Name() + " stopped at " + Thousands(p.features_cached) + " features";
    if (!p.last_error.empty() && failed.empty()) failed = f->Name() + ": " + p.last_error;
  }
  if (tiles > 0) return "Loading " + std::to_string(tiles) + (tiles == 1 ? " web tile" : " web tiles");
  if (!capped.empty()) return capped;
  return failed;
}

}  // namespace desk
}  // namespace fv
