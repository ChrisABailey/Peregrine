// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// MM6: the line transports. LineBuffer's framing gets the most attention here
// because it is the one piece every transport shares and the one place a
// hand-rolled reader always gets the split-read case wrong.

#include "fvkit/nav/line_transport.h"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace fv {
namespace {

std::string ScratchPath(const char* name) {
  const char* dir = std::getenv("TMPDIR");
  std::string base = (dir != nullptr && dir[0] != '\0') ? dir : "/tmp";
  if (base.back() != '/') base += '/';
  return base + name;
}

// ---------------------------------------------------------------------------
// LineBuffer
// ---------------------------------------------------------------------------

TEST(LineBuffer, SplitsOnNewlineAndDropsCarriageReturn) {
  LineBuffer buffer;
  buffer.Append(std::string("one\r\ntwo\nthree\r\n"));

  std::string line;
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "one");
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "two");
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "three");
  EXPECT_FALSE(buffer.NextLine(&line));
}

// The case every hand-rolled reader gets wrong: bytes arriving in chunks that
// do not respect line boundaries.
TEST(LineBuffer, HoldsAPartialLineAcrossAppends) {
  LineBuffer buffer;
  std::string line;

  buffer.Append(std::string("$GPRMC,12"));
  EXPECT_FALSE(buffer.NextLine(&line));
  EXPECT_EQ(buffer.pending_bytes(), 9u);

  buffer.Append(std::string("3456.00,A*7C\r\n$GPG"));
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "$GPRMC,123456.00,A*7C");
  EXPECT_FALSE(buffer.NextLine(&line));

  buffer.Append(std::string("GA,x\n"));
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "$GPGGA,x");
}

TEST(LineBuffer, SkipsBlankLines) {
  LineBuffer buffer;
  buffer.Append(std::string("a\n\n\r\n\nb\n"));

  std::string line;
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "a");
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "b");
  EXPECT_FALSE(buffer.NextLine(&line));
}

TEST(LineBuffer, TakePartialYieldsAnUnterminatedTailOnce) {
  LineBuffer buffer;
  buffer.Append(std::string("done\nlast"));

  std::string line;
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "done");
  ASSERT_TRUE(buffer.TakePartial(&line));
  EXPECT_EQ(line, "last");
  EXPECT_FALSE(buffer.TakePartial(&line));
}

// The bound that keeps a hostile stream from becoming an unbounded
// allocation. The overlong line is dropped whole, and the NEXT line still
// arrives — a reader that resynchronised wrongly would lose that one too.
TEST(LineBuffer, DropsAnOverlongLineAndResynchronises) {
  LineBuffer buffer(16);
  buffer.Append(std::string(200, 'x'));

  std::string line;
  EXPECT_FALSE(buffer.NextLine(&line));
  EXPECT_EQ(buffer.overlong_dropped(), 1u);
  EXPECT_LE(buffer.pending_bytes(), 16u);

  buffer.Append(std::string("xxxx\nshort\n"));
  ASSERT_TRUE(buffer.NextLine(&line));
  EXPECT_EQ(line, "short");
  EXPECT_EQ(buffer.overlong_dropped(), 1u);
}

// ---------------------------------------------------------------------------
// StringLineTransport
// ---------------------------------------------------------------------------

TEST(StringLineTransport, EndsWhenTheCannedDataRunsOut) {
  StringLineTransport transport("a\nb\n");
  ASSERT_TRUE(transport.Open().ok());

  std::string line;
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "a");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "b");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kEnd);
}

// auto_end off is the live-feed shape: out of data is kAgain, not kEnd.
TEST(StringLineTransport, StaysLiveUntilToldOtherwise) {
  StringLineTransport transport("a\n", /*auto_end=*/false);
  ASSERT_TRUE(transport.Open().ok());

  std::string line;
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kAgain);

  transport.AddData("b\n");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "b");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kAgain);

  transport.SetEnded();
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kEnd);
}

// ---------------------------------------------------------------------------
// FileLineTransport
// ---------------------------------------------------------------------------

TEST(FileLineTransport, MissingFileIsNotFound) {
  FileLineTransport transport(ScratchPath("fv_no_such_nmea_log.txt"));
  const Status status = transport.Open();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code, kNotFound);
}

TEST(FileLineTransport, ReadsEveryLineIncludingAnUnterminatedLast) {
  const std::string path = ScratchPath("fv_line_transport_test.log");
  {
    std::ofstream out(path, std::ios::binary);
    out << "first\r\nsecond\r\nthird";  // no terminator on the last one
  }

  FileLineTransport transport(path);
  ASSERT_TRUE(transport.Open().ok());
  std::string line;
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "first");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "second");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "third");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kEnd);
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kEnd);  // and stays ended

  transport.Close();
  std::remove(path.c_str());
}

// `follow` is the tail: end of data is kAgain, and what is appended afterwards
// arrives.
TEST(FileLineTransport, FollowPicksUpAppendedLines) {
  const std::string path = ScratchPath("fv_line_transport_follow.log");
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "one\n";
  }

  FileLineTransport transport(path, /*follow=*/true);
  ASSERT_TRUE(transport.Open().ok());
  std::string line;
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "one");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kAgain);

  {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << "two\n";
  }
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
  EXPECT_EQ(line, "two");
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kAgain);

  transport.Close();
  std::remove(path.c_str());
}

// Re-opening replays from the top, which is what makes a demo re-runnable.
TEST(FileLineTransport, ReopenReplaysFromTheTop) {
  const std::string path = ScratchPath("fv_line_transport_replay.log");
  {
    std::ofstream out(path, std::ios::binary);
    out << "a\nb\n";
  }

  FileLineTransport transport(path);
  std::string line;
  for (int pass = 0; pass < 2; ++pass) {
    ASSERT_TRUE(transport.Open().ok());
    EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
    EXPECT_EQ(line, "a");
    EXPECT_EQ(transport.ReadLine(&line), LineResult::kLine);
    EXPECT_EQ(line, "b");
    EXPECT_EQ(transport.ReadLine(&line), LineResult::kEnd);
    transport.Close();
  }
  std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// UDP — the one socket path a test can drive without a peer to talk to
// ---------------------------------------------------------------------------

#ifndef _WIN32
TEST(UdpLineTransport, ReceivesDatagramsAndFramesAcrossThem) {
  UdpLineTransport transport(0);  // an ephemeral port, so parallel runs cannot clash
  const Status opened = transport.Open();
  ASSERT_TRUE(opened.ok()) << opened.message;
  ASSERT_NE(transport.bound_port(), 0);

  std::string line;
  EXPECT_EQ(transport.ReadLine(&line), LineResult::kAgain);  // silence is not an end

  const int sender = ::socket(AF_INET, SOCK_DGRAM, 0);
  ASSERT_GE(sender, 0);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(transport.bound_port());
  ASSERT_EQ(::inet_pton(AF_INET, "127.0.0.1", &to.sin_addr), 1);

  // One sentence split across two datagrams, then a whole one: the framing
  // has to hold the tail of the first packet.
  const std::string first = "$GPGLL,3236.269,N,0800";
  const std::string second = "4.744,W,205559.00,A*3B\r\n$GPVTG,,,,,5.00,N,,K*11\r\n";
  ASSERT_GT(::sendto(sender, first.data(), first.size(), 0,
                     reinterpret_cast<sockaddr*>(&to), sizeof(to)),
            0);
  ASSERT_GT(::sendto(sender, second.data(), second.size(), 0,
                     reinterpret_cast<sockaddr*>(&to), sizeof(to)),
            0);

  // Loopback delivery is effectively immediate, but a bounded retry keeps the
  // test from being a scheduling bet.
  std::string got;
  LineResult result = LineResult::kAgain;
  for (int attempt = 0; attempt < 200 && result != LineResult::kLine; ++attempt) {
    result = transport.ReadLine(&got);
  }
  ASSERT_EQ(result, LineResult::kLine);
  EXPECT_EQ(got, "$GPGLL,3236.269,N,08004.744,W,205559.00,A*3B");

  result = LineResult::kAgain;
  for (int attempt = 0; attempt < 200 && result != LineResult::kLine; ++attempt) {
    result = transport.ReadLine(&got);
  }
  ASSERT_EQ(result, LineResult::kLine);
  EXPECT_EQ(got, "$GPVTG,,,,,5.00,N,,K*11");

  ::close(sender);
  transport.Close();
}
#endif  // !_WIN32

TEST(TcpLineTransport, RefusesToResolveNonsense) {
  TcpLineTransport transport("no.such.host.invalid", 10110);
  const Status status = transport.Open();
  EXPECT_FALSE(status.ok());
  EXPECT_FALSE(transport.is_open());
  EXPECT_EQ(transport.description(), "tcp:no.such.host.invalid:10110");
}

}  // namespace
}  // namespace fv
