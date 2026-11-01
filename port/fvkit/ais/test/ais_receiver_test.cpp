// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// Tests for the local AIS receiver: line routing, HDT, the reconnect
/// schedule, and real TCP and UDP sockets on loopback.

#include "fvkit/ais/receiver.h"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <memory>
#include <string>

namespace fv {
namespace ais {
namespace {

/// `$` + body + `*hh`.
std::string Sentence(const std::string& body) {
  char sum[4];
  std::snprintf(sum, sizeof(sum), "%02X", NmeaChecksum(body));
  return "$" + body + "*" + sum;
}

const char kVdm[] = "!AIVDM,1,1,,A,15M2oq@01TrBW?0BeP`;f9H00000,0*35";
const char kTagged[] = "\\s:SYNTH01,c:1793016000*6E\\!AIVDM,1,1,,A,403Owsivbe<00rB70PBhNN700000,0*34";

/// A receiver over one StringLineTransport the test keeps a handle on; each
/// reopen hands out a fresh live transport.
struct Rig {
  std::shared_ptr<StringLineTransport> transport;
  int opens = 0;
  NmeaReceiver receiver{[this]() -> std::shared_ptr<ILineTransport> {
    ++opens;
    transport = std::make_shared<StringLineTransport>(std::string(), false);
    return transport;
  }};
};

TEST(AisReceiver, RecognisesAisSentences) {
  EXPECT_TRUE(IsAisSentence(kVdm));
  EXPECT_TRUE(IsAisSentence(kTagged));
  EXPECT_TRUE(IsAisSentence("!AIVDO,1,1,,A,15M2sIh00`rBEb0Bge=G25`20000,0*25"));
  EXPECT_FALSE(IsAisSentence("!AIALR,000000.00,001,A,V,AIS*00"));
  EXPECT_FALSE(IsAisSentence(Sentence("GPRMC,205559.00,A")));
  EXPECT_FALSE(IsAisSentence("\\s:unterminated"));
}

TEST(AisReceiver, ParsesTrueHeading) {
  double h = -1.0;
  EXPECT_TRUE(ParseNmeaTrueHeading(Sentence("HEHDT,274.5,T"), &h));
  EXPECT_DOUBLE_EQ(h, 274.5);
  EXPECT_TRUE(ParseNmeaTrueHeading(Sentence("IIHDT,0.0,T"), &h));
  EXPECT_DOUBLE_EQ(h, 0.0);
  EXPECT_FALSE(ParseNmeaTrueHeading(Sentence("HEHDT,,T"), &h));
  EXPECT_FALSE(ParseNmeaTrueHeading(Sentence("HEHDT,400,T"), &h));
  EXPECT_FALSE(ParseNmeaTrueHeading("$HEHDT,274.5,T*00", &h));  // bad checksum
  EXPECT_FALSE(ParseNmeaTrueHeading(Sentence("HEHDM,274.5,M"), &h));
}

TEST(AisReceiver, RoutesAisAndPositionLines) {
  Rig rig;
  rig.receiver.Start(1000.0);
  EXPECT_EQ(rig.receiver.state(), ReceiverState::kConnecting);

  rig.transport->AddData(std::string(kVdm) + "\r\n" + kTagged + "\r\n");
  rig.transport->AddData(Sentence("GPRMC,205559.00,A,3236.2694,N,08004.7436,W,12.50,187.30,261026,,") +
                         "\r\n");
  rig.transport->AddData(Sentence("GPRMC,205600.00,A,3236.2794,N,08004.7436,W,12.50,187.30,261026,,") +
                         "\r\n");
  rig.transport->AddData("$PSRT,garbage\r\n");
  ReceiverBatch batch;
  rig.receiver.Poll(1001.0, &batch);

  EXPECT_EQ(rig.receiver.state(), ReceiverState::kLive);
  EXPECT_TRUE(batch.state_changed);
  ASSERT_EQ(batch.ais_lines.size(), 2u);
  EXPECT_EQ(batch.ais_lines[1], kTagged);
  // The first epoch closes when the second starts.
  ASSERT_EQ(batch.fixes.size(), 1u);
  EXPECT_TRUE(batch.fixes[0].has_position);
  EXPECT_NEAR(batch.fixes[0].lat, 32.0 + 36.2694 / 60.0, 1e-9);
  EXPECT_NEAR(batch.fixes[0].true_heading_deg, 187.3, 1e-9);
  EXPECT_EQ(rig.receiver.lines_received(), 5u);
  EXPECT_EQ(rig.receiver.ais_lines(), 2u);
  EXPECT_EQ(rig.receiver.lines_ignored(), 1u);
}

TEST(AisReceiver, HdtFillsTheHeadingOfASlowOrHeadinglessFix) {
  Rig rig;
  rig.receiver.Start(1000.0);
  rig.transport->AddData(Sentence("HEHDT,90.0,T") + "\n");
  // At rest: course 187 from the RMC is replaced by the ship's heading.
  rig.transport->AddData(Sentence("GPRMC,205559.00,A,3236.2694,N,08004.7436,W,0.1,187.30,261026,,") +
                         "\n");
  // GGA only, no course at all: takes the heading too.
  rig.transport->AddData(Sentence("GPGGA,205600.00,3236.2694,N,08004.7436,W,1,08,0.9,9.4,M,,M,,") +
                         "\n");
  rig.transport->AddData(Sentence("GPGGA,205601.00,3236.2694,N,08004.7436,W,1,08,0.9,9.4,M,,M,,") +
                         "\n");
  ReceiverBatch batch;
  rig.receiver.Poll(1001.0, &batch);
  ASSERT_EQ(batch.fixes.size(), 2u);
  EXPECT_DOUBLE_EQ(batch.fixes[0].true_heading_deg, 90.0);
  EXPECT_TRUE(batch.fixes[1].has_true_heading);
  EXPECT_DOUBLE_EQ(batch.fixes[1].true_heading_deg, 90.0);
  // GGA has no date; the RMC's stamps it.
  EXPECT_TRUE(batch.fixes[1].has_time);
  EXPECT_DOUBLE_EQ(batch.fixes[1].time_s, UtcToEpochSeconds(2026, 10, 26, 20 * 3600 + 56 * 60));
}

TEST(AisReceiver, MovingFixKeepsItsCourse) {
  Rig rig;
  rig.receiver.Start(1000.0);
  rig.transport->AddData(Sentence("HEHDT,90.0,T") + "\n");
  rig.transport->AddData(Sentence("GPRMC,205559.00,A,3236.2694,N,08004.7436,W,6.0,187.30,261026,,") +
                         "\n");
  ReceiverBatch batch;
  rig.receiver.Poll(1001.0, &batch);
  rig.receiver.Stop();
  EXPECT_TRUE(batch.fixes.empty());  // the epoch is still open; Stop drops it

  rig.receiver.Start(2000.0);
  rig.transport->AddData(Sentence("HEHDT,90.0,T") + "\n");
  rig.transport->AddData(Sentence("GPRMC,205559.00,A,3236.2694,N,08004.7436,W,6.0,187.30,261026,,") +
                         "\n");
  rig.transport->SetEnded();
  batch = ReceiverBatch();
  rig.receiver.Poll(2001.0, &batch);
  ASSERT_EQ(batch.fixes.size(), 1u);  // flushed at end of stream
  EXPECT_NEAR(batch.fixes[0].true_heading_deg, 187.3, 1e-9);
}

const char kVdo[] = "!AIVDO,1,1,,A,15M2sIh00`rBEb0Bge=G25`20000,0*25";

TEST(AisReceiver, VdoGivesAFixWhenThereIsNoGps) {
  Rig rig;
  rig.receiver.Start(1000.0);
  rig.transport->AddData(std::string(kVdo) + "\n" + kVdm + "\n");
  ReceiverBatch batch;
  rig.receiver.Poll(1001.0, &batch);
  EXPECT_EQ(batch.ais_lines.size(), 2u);  // still drawn-or-not by the overlay's own rules
  ASSERT_EQ(batch.fixes.size(), 1u);
  const PositionFix& f = batch.fixes[0];
  EXPECT_NEAR(f.lat, 32.7598, 1e-3);
  EXPECT_NEAR(f.lon, -79.88, 1e-3);
  EXPECT_TRUE(f.has_speed);
  EXPECT_NEAR(f.speed_mps, 4.0 * 1852.0 / 3600.0, 1e-6);
  EXPECT_NEAR(f.true_heading_deg, 180.0, 1e-9);
  EXPECT_DOUBLE_EQ(f.time_s, 1001.0);
  EXPECT_EQ(rig.receiver.vdo_fixes(), 1u);
}

TEST(AisReceiver, GpsSentencesTakePrecedenceOverVdo) {
  Rig rig;
  rig.receiver.Start(1000.0);
  rig.transport->AddData(Sentence("GPRMC,205559.00,A,3236.2694,N,08004.7436,W,6.0,90.0,261026,,") + "\n");
  rig.transport->AddData(Sentence("GPRMC,205600.00,A,3236.2694,N,08004.7436,W,6.0,90.0,261026,,") + "\n");
  ReceiverBatch batch;
  rig.receiver.Poll(1001.0, &batch);
  ASSERT_EQ(batch.fixes.size(), 1u);

  // A VDO within 10 s of the GPS fix is not a position.
  rig.transport->AddData(std::string(kVdo) + "\n");
  batch = ReceiverBatch();
  rig.receiver.Poll(1010.0, &batch);
  EXPECT_TRUE(batch.fixes.empty());

  // The GPS has gone quiet: the transponder takes over.
  rig.transport->AddData(std::string(kVdo) + "\n");
  batch = ReceiverBatch();
  rig.receiver.Poll(1011.5, &batch);
  ASSERT_EQ(batch.fixes.size(), 1u);
  EXPECT_NEAR(batch.fixes[0].lat, 32.7598, 1e-3);
  EXPECT_EQ(rig.receiver.vdo_fixes(), 1u);
}

TEST(AisReceiver, VdmIsNeverAPosition) {
  Rig rig;
  rig.receiver.Start(1000.0);
  rig.transport->AddData(std::string(kVdm) + "\n" + kTagged + "\n");
  ReceiverBatch batch;
  rig.receiver.Poll(1001.0, &batch);
  EXPECT_TRUE(batch.fixes.empty());
}

TEST(AisReceiver, EndRetriesWithBackoffAndALineResetsIt) {
  Rig rig;
  rig.receiver.Start(0.0);
  EXPECT_EQ(rig.opens, 1);

  ReceiverBatch batch;
  rig.transport->SetEnded();
  rig.receiver.Poll(1.0, &batch);
  EXPECT_EQ(rig.receiver.state(), ReceiverState::kRetrying);
  EXPECT_EQ(rig.receiver.last_error(), "connection closed by the receiver");
  EXPECT_DOUBLE_EQ(rig.receiver.retry_at_s(), 3.0);

  rig.receiver.Poll(2.9, &batch);
  EXPECT_EQ(rig.opens, 1);
  rig.receiver.Poll(3.0, &batch);
  EXPECT_EQ(rig.opens, 2);
  EXPECT_EQ(rig.receiver.state(), ReceiverState::kConnecting);

  rig.transport->SetEnded();
  rig.receiver.Poll(3.5, &batch);
  EXPECT_DOUBLE_EQ(rig.receiver.retry_at_s(), 7.5);  // 4 s now

  rig.receiver.Poll(7.5, &batch);
  rig.transport->AddData(std::string(kVdm) + "\n");
  rig.transport->SetEnded();
  rig.receiver.Poll(8.0, &batch);
  EXPECT_DOUBLE_EQ(rig.receiver.retry_at_s(), 10.0);  // back to 2 s after a line
}

TEST(AisReceiver, SilenceAndDeadTcp) {
  Rig rig;
  rig.receiver.Start(0.0);
  ReceiverBatch batch;
  rig.transport->AddData(std::string(kVdm) + "\n");
  rig.receiver.Poll(1.0, &batch);
  EXPECT_EQ(rig.receiver.state(), ReceiverState::kLive);
  rig.receiver.Poll(16.5, &batch);
  EXPECT_EQ(rig.receiver.state(), ReceiverState::kSilent);
  rig.receiver.Poll(62.0, &batch);
  EXPECT_EQ(rig.receiver.state(), ReceiverState::kRetrying);
  EXPECT_EQ(rig.receiver.last_error(), "no data");
}

TEST(AisReceiver, OpenFailureRetries) {
  int opens = 0;
  NmeaReceiver receiver([&opens]() -> std::shared_ptr<ILineTransport> {
    ++opens;
    return std::make_shared<TcpLineTransport>("no.such.host.invalid", 10110);
  });
  receiver.Start(0.0);
  EXPECT_EQ(receiver.state(), ReceiverState::kRetrying);
  EXPECT_FALSE(receiver.last_error().empty());
  ReceiverBatch batch;
  receiver.Poll(2.0, &batch);
  EXPECT_EQ(opens, 2);
  EXPECT_DOUBLE_EQ(receiver.retry_at_s(), 6.0);
  receiver.Stop();
  EXPECT_EQ(receiver.state(), ReceiverState::kOff);
}

#ifndef _WIN32
/// Polls until `done` or about a second has passed.
template <typename Done>
void PollUntil(NmeaReceiver* receiver, ReceiverBatch* batch, Done done) {
  for (int i = 0; i < 1000 && !done(); ++i) {
    receiver->Poll(1.0, batch);
    ::usleep(1000);
  }
}

TEST(AisReceiver, TcpLoopback) {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listener, 0);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr), 1);
  ASSERT_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  ASSERT_EQ(::listen(listener, 1), 0);
  socklen_t len = sizeof(addr);
  ASSERT_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len), 0);

  ReceiverConfig config;
  config.protocol = ReceiverProtocol::kTcp;
  config.host = "127.0.0.1";
  config.port = ntohs(addr.sin_port);
  NmeaReceiver receiver(NmeaReceiver::FactoryFor(config));
  receiver.Start(0.0);
  EXPECT_EQ(receiver.description(), "tcp:127.0.0.1:" + std::to_string(config.port));

  const int peer = ::accept(listener, nullptr, nullptr);
  ASSERT_GE(peer, 0);
  const std::string data = std::string(kVdm) + "\r\n";
  ASSERT_EQ(::send(peer, data.data(), data.size(), 0), static_cast<ssize_t>(data.size()));
  ::close(peer);

  ReceiverBatch batch;
  PollUntil(&receiver, &batch,
            [&]() { return receiver.state() == ReceiverState::kRetrying; });
  ASSERT_EQ(batch.ais_lines.size(), 1u);
  EXPECT_EQ(batch.ais_lines[0], kVdm);
  EXPECT_EQ(receiver.state(), ReceiverState::kRetrying);
  ::close(listener);
}

TEST(AisReceiver, UdpLoopback) {
  std::shared_ptr<UdpLineTransport> transport;
  NmeaReceiver receiver([&transport]() -> std::shared_ptr<ILineTransport> {
    transport = std::make_shared<UdpLineTransport>(0);
    return transport;
  });
  receiver.Start(0.0);
  ASSERT_EQ(receiver.state(), ReceiverState::kConnecting);
  ASSERT_NE(transport->bound_port(), 0);

  const int sender = ::socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sender, 0);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(transport->bound_port());
  ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &to.sin_addr), 1);
  const std::string data = std::string(kTagged) + "\r\n";
  ASSERT_GT(::sendto(sender, data.data(), data.size(), 0, reinterpret_cast<sockaddr*>(&to),
                     sizeof(to)),
            0);

  ReceiverBatch batch;
  PollUntil(&receiver, &batch, [&]() { return !batch.ais_lines.empty(); });
  ASSERT_EQ(batch.ais_lines.size(), 1u);
  EXPECT_EQ(receiver.state(), ReceiverState::kLive);

  // UDP never times out into a reconnect.
  receiver.Poll(500.0, &batch);
  EXPECT_EQ(receiver.state(), ReceiverState::kSilent);
  ::close(sender);
}
#endif  // !_WIN32

}  // namespace
}  // namespace ais
}  // namespace fv
