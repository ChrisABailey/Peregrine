// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the window's shared NMEA connections (`FeedReceivers`).

#include "fv_desk_feeds.h"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "fvkit/nav/line_transport.h"

namespace fv {
namespace desk {
namespace {

const char kRmc[] =
    "$GPRMC,205559.00,A,3236.2694,N,08004.7436,W,6.0,187.30,261026,,*12\n";

/// Hands out a fresh live string transport per open, keyed by port, and
/// counts the opens.
struct FakeEndpoints {
  std::map<uint16_t, std::shared_ptr<StringLineTransport>> latest;
  int opens = 0;
  FeedReceivers::FactoryMaker Maker() {
    return [this](const ReceiverConfig& c) -> NmeaReceiver::TransportFactory {
      return [this, port = c.port]() -> std::shared_ptr<ILineTransport> {
        ++opens;
        auto t = std::make_shared<StringLineTransport>(std::string(), false);
        latest[port] = t;
        return t;
      };
    };
  }
};

ReceiverConfig Tcp(uint16_t port) {
  ReceiverConfig c;
  c.host = "192.0.2.1";
  c.port = port;
  return c;
}

TEST(FeedReceivers, MapsOnOneEndpointShareOneConnection) {
  FakeEndpoints fake;
  FeedReceivers feeds(fake.Maker());
  auto a = feeds.Subscribe(Tcp(10110), 1000.0);
  auto b = feeds.Subscribe(Tcp(10110), 1000.0);
  EXPECT_EQ(fake.opens, 1);
  EXPECT_EQ(feeds.open_count(), 1u);
  EXPECT_EQ(&a->receiver(), &b->receiver());

  fake.latest[10110]->AddData(kRmc);
  feeds.Poll(1001.0);
  EXPECT_EQ(a->TakeFixes().size(), 1u);
  EXPECT_EQ(b->TakeFixes().size(), 1u);
  EXPECT_TRUE(a->TakeFixes().empty());

  auto c = feeds.Subscribe(Tcp(10111), 1000.0);
  EXPECT_EQ(feeds.open_count(), 2u);
  a.reset();
  EXPECT_EQ(feeds.open_count(), 2u);  // b still listens
  b.reset();
  c.reset();
  EXPECT_EQ(feeds.open_count(), 0u);
}

TEST(FeedReceivers, ADroppedTcpConnectionReconnects) {
  FakeEndpoints fake;
  FeedReceivers feeds(fake.Maker());
  auto sub = feeds.Subscribe(Tcp(10110), 1000.0);
  fake.latest[10110]->SetEnded();
  feeds.Poll(1001.0);
  EXPECT_EQ(sub->receiver().state(), ReceiverState::kRetrying);

  feeds.Poll(1001.0 + 2.5);  // past the first 2 s backoff
  EXPECT_EQ(fake.opens, 2);
  fake.latest[10110]->AddData(kRmc);
  feeds.Poll(1004.0);
  EXPECT_EQ(sub->receiver().state(), ReceiverState::kLive);
  EXPECT_EQ(sub->TakeFixes().size(), 1u);
}

}  // namespace
}  // namespace desk
}  // namespace fv
