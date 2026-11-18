// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fv_desk_feeds.h"

#include <algorithm>
#include <utility>

namespace fv {
namespace desk {

std::vector<PositionFix> FeedSubscription::TakeFixes() {
  std::vector<PositionFix> out;
  out.swap(pending_);
  return out;
}

FeedReceivers::FeedReceivers(FactoryMaker maker) : maker_(std::move(maker)) {}

FeedReceivers::~FeedReceivers() {
  for (Endpoint& e : endpoints_) e.receiver->Stop();
}

std::shared_ptr<FeedSubscription> FeedReceivers::Subscribe(const ReceiverConfig& config,
                                                           double now_s) {
  Prune();
  auto it = std::find_if(endpoints_.begin(), endpoints_.end(),
                         [&config](const Endpoint& e) { return e.config == config; });
  if (it == endpoints_.end()) {
    Endpoint e;
    e.config = config;
    e.receiver = std::make_shared<NmeaReceiver>(maker_(config));
    // A moving map shows each sentence's position as it arrives.
    e.receiver->SetEmitPerSentence(true);
    e.receiver->Start(now_s);
    endpoints_.push_back(std::move(e));
    it = endpoints_.end() - 1;
  }
  auto sub = std::make_shared<FeedSubscription>(it->receiver);
  it->subscribers.push_back(sub);
  return sub;
}

void FeedReceivers::Poll(double now_s) {
  Prune();
  for (Endpoint& e : endpoints_) {
    ReceiverBatch batch;
    e.receiver->Poll(now_s, &batch);
    if (batch.fixes.empty()) continue;
    for (const std::weak_ptr<FeedSubscription>& w : e.subscribers)
      if (std::shared_ptr<FeedSubscription> sub = w.lock())
        sub->pending_.insert(sub->pending_.end(), batch.fixes.begin(), batch.fixes.end());
  }
}

std::size_t FeedReceivers::open_count() {
  Prune();
  return endpoints_.size();
}

void FeedReceivers::Prune() {
  for (Endpoint& e : endpoints_)
    e.subscribers.erase(std::remove_if(e.subscribers.begin(), e.subscribers.end(),
                                       [](const auto& w) { return w.expired(); }),
                        e.subscribers.end());
  const auto unused = std::remove_if(endpoints_.begin(), endpoints_.end(), [](Endpoint& e) {
    if (!e.subscribers.empty()) return false;
    e.receiver->Stop();
    return true;
  });
  endpoints_.erase(unused, endpoints_.end());
}

}  // namespace desk
}  // namespace fv
