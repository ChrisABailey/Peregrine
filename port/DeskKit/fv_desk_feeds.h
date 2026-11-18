// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_feeds.h — the window's open NMEA connections, one per endpoint.
///
/// Moving maps naming the same protocol, host and port share one
/// `NmeaReceiver`, and so one socket. UI thread only.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

#include "fvkit/nav/nmea_receiver.h"
#include "fvkit/nav/position.h"

namespace fv {
namespace desk {

/// One listener's share of an open connection: the fixes it has not taken yet.
class FeedSubscription {
 public:
  explicit FeedSubscription(std::shared_ptr<NmeaReceiver> receiver)
      : receiver_(std::move(receiver)) {}

  /// The fixes received since the last call, oldest first.
  std::vector<PositionFix> TakeFixes();
  /// The shared connection, for its state and counters.
  const NmeaReceiver& receiver() const { return *receiver_; }

 private:
  friend class FeedReceivers;
  std::shared_ptr<NmeaReceiver> receiver_;
  std::vector<PositionFix> pending_;
};

/// The open connections. A connection closes once no subscription to it is left.
class FeedReceivers {
 public:
  /// Makes the transport factory for an endpoint; tests substitute fakes.
  using FactoryMaker = std::function<NmeaReceiver::TransportFactory(const ReceiverConfig&)>;

  explicit FeedReceivers(FactoryMaker maker = NmeaReceiver::FactoryFor);
  ~FeedReceivers();
  FeedReceivers(const FeedReceivers&) = delete;
  FeedReceivers& operator=(const FeedReceivers&) = delete;

  /// Subscribes to `config`'s endpoint, opening it at `now_s` unless it is
  /// already open.
  std::shared_ptr<FeedSubscription> Subscribe(const ReceiverConfig& config, double now_s);
  /// Reads every open connection once and hands each fix to all its
  /// subscriptions. A dropped TCP connection reopens on the receiver's backoff.
  void Poll(double now_s);
  /// How many connections are open.
  std::size_t open_count();

 private:
  struct Endpoint {
    ReceiverConfig config;
    std::shared_ptr<NmeaReceiver> receiver;
    std::vector<std::weak_ptr<FeedSubscription>> subscribers;
  };
  /// Closes the connections nobody listens to any more.
  void Prune();

  FactoryMaker maker_;
  std::vector<Endpoint> endpoints_;
};

}  // namespace desk
}  // namespace fv
