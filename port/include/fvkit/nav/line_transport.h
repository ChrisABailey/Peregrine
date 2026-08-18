// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

// fvkit/nav/line_transport.h — where a line of text comes from (nav plan MM6).
//
// NMEA is a stream of newline-terminated ASCII sentences and nothing more, so
// the only thing an NMEA source needs from the world is "give me the next
// line". That is this file, and it is a SEPARATE seam from the parser on
// purpose: `nmea.h` then has no idea whether the bytes came off a socket, out
// of a recorded log or out of a string literal in a test, and every transport
// is testable without a parser.
//
// This replaces `NetNMEA/comm.cpp` + `poller.cpp` (Win32 overlapped serial I/O
// and an MFC message pump) rather than porting them: FalconView's transport is
// a COM object driving a `HANDLE`, and severing it is D6's adapter seam.
//
// THREE RULES.
//
// 1. READLINE NEVER BLOCKS. It returns `kAgain` when nothing has arrived yet,
//    so a shell can call it from the tick it already has and no source in this
//    layer needs a thread. (A transport that WANTS a thread is still legal —
//    it just fills a FixQueue instead, exactly as position.h describes.)
//
// 2. FRAMING IS ONE IMPLEMENTATION. `LineBuffer` holds the tail of a partial
//    read and hands out whole lines; every transport pushes bytes into it and
//    none of them re-derive "what if the recv split the sentence in half".
//    That split is the bug every hand-rolled reader has, so it is written once
//    and tested directly.
//
// 3. END OF STREAM IS NOT AN ERROR. A recorded log ends; a socket the far end
//    closed ends. `kEnd` is that, and it is distinct from `kError`, because a
//    source flushes its pending fix on the first and stops on the second.

#ifndef FVKIT_NAV_LINE_TRANSPORT_H_
#define FVKIT_NAV_LINE_TRANSPORT_H_

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

#include "fvkit/geo.h"

namespace fv {

// What one ReadLine call did. Not a `Status` (D3) because three of the four
// outcomes are not failures: `kAgain` is the normal state of a live feed
// between sentences, and `kEnd` is how a recording says it is over.
enum class LineResult {
  kLine,   // *out holds one line, its terminator stripped
  kAgain,  // nothing available yet; ask again on the next tick
  kEnd,    // stream is over and will produce nothing further
  kError,  // the transport broke; error() says how
};

// The tail-holding line splitter every transport shares.
//
// Accepts bytes in whatever chunks the world delivers them and hands back
// whole lines. Splits on '\n', drops a trailing '\r' (NMEA's terminator is
// <CR><LF>, and a log written on one platform and read on another may carry
// either), and DROPS EMPTY LINES — a blank line is never a sentence and making
// every caller skip it is how one of them forgets.
//
// A line longer than `max_line_length` is discarded rather than grown into:
// the cap is what stops a socket sending one endless line from becoming an
// unbounded allocation, which matters because this is the port's first
// network-facing reader.
class LineBuffer {
 public:
  // 512 is six times the longest legal NMEA sentence (82), so a real sentence
  // never trips it and a garbage stream is still bounded.
  explicit LineBuffer(std::size_t max_line_length = 512)
      : max_line_length_(max_line_length ? max_line_length : 1) {}

  void Append(const char* data, std::size_t length);
  void Append(const std::string& data) { Append(data.data(), data.size()); }

  // Takes the next whole line, if there is one. False leaves *out alone.
  bool NextLine(std::string* out);

  // Whatever bytes are left with no terminator after them. A recording whose
  // last line has no newline still yields its last sentence through this, and
  // it empties the buffer. False when there is nothing left.
  bool TakePartial(std::string* out);

  void Clear();

  std::size_t pending_bytes() const { return buffer_.size() - consumed_; }
  // Lines dropped for exceeding max_line_length.
  std::size_t overlong_dropped() const { return overlong_dropped_; }

 private:
  void Compact();

  std::string buffer_;
  std::size_t consumed_ = 0;  // bytes of buffer_ already handed out
  std::size_t max_line_length_;
  std::size_t overlong_dropped_ = 0;
  bool discarding_ = false;  // inside an overlong line, skipping to its '\n'
};

// A source of lines. Held as std::shared_ptr per D1: the source that reads it
// holds it, and a test holds the same object to feed it.
class ILineTransport {
 public:
  virtual ~ILineTransport() = default;
  ILineTransport(const ILineTransport&) = delete;
  ILineTransport& operator=(const ILineTransport&) = delete;

  // Idempotent, both of them. Re-opening a closed transport restarts it (a
  // file plays again from the top), which is what makes a replay re-runnable.
  virtual Status Open() = 0;
  virtual void Close() = 0;
  virtual bool is_open() const = 0;

  virtual LineResult ReadLine(std::string* out) = 0;

  // Set when ReadLine last returned kError. Never cleared by a later kAgain,
  // so a shell that only checks it at the end still sees what happened.
  const Status& error() const { return error_; }

  // A human-readable name for logs and for a shell's status line.
  virtual std::string description() const = 0;

 protected:
  ILineTransport() = default;
  Status error_;
};

// ---------------------------------------------------------------------------
// The test double, and the "I already have the bytes" case
// ---------------------------------------------------------------------------

// Lines out of a string. `AddData` appends, so a test can hand over half a
// sentence, assert kAgain, then hand over the rest — which is the only way to
// prove the framing actually holds a partial read.
//
// `set_end_of_stream(false)` (the default is true once the data runs out only
// if `auto_end` is set) keeps it live: a transport that has run out of canned
// data but has not ended answers kAgain, which is what a socket does.
class StringLineTransport : public ILineTransport {
 public:
  StringLineTransport() = default;
  explicit StringLineTransport(std::string data, bool auto_end = true);

  void AddData(const std::string& data);

  // Say the stream is over. Any bytes already added are still handed out
  // first, including a final line with no terminator.
  void SetEnded();

  Status Open() override;
  void Close() override;
  bool is_open() const override { return open_; }
  LineResult ReadLine(std::string* out) override;
  std::string description() const override { return "string"; }

  LineBuffer& buffer() { return buffer_; }

 private:
  LineBuffer buffer_;
  bool open_ = false;
  bool auto_end_ = true;
  bool ended_ = false;
};

// ---------------------------------------------------------------------------
// A recorded log
// ---------------------------------------------------------------------------

// Lines out of a file. This is the plan's "playback of a recorded log", and it
// deliberately does NOT pace itself: it hands over lines as fast as it is
// asked. Pacing a recording by its own timestamps is `ScriptedSource`'s job
// and it already does it exactly — read the log into fixes (`ReadNmeaLog`),
// build a script from their own stamps (`BuildScriptedTrackFromFixes`), play
// it. What this transport is for is the OTHER two cases: a fast-as-possible
// replay through the live path, and `follow` below.
class FileLineTransport : public ILineTransport {
 public:
  // `follow` tails the file: at end of data it answers kAgain instead of kEnd
  // and picks up whatever has since been appended, so `nc host port >> log`
  // in one window feeds the map in another. Without it a file ends.
  explicit FileLineTransport(std::string path, bool follow = false);

  Status Open() override;
  void Close() override;
  bool is_open() const override { return stream_.is_open(); }
  LineResult ReadLine(std::string* out) override;
  std::string description() const override { return "file:" + path_; }

  const std::string& path() const { return path_; }
  bool follow() const { return follow_; }

 private:
  std::string path_;
  bool follow_;
  std::ifstream stream_;
  LineBuffer buffer_;
  // Bytes read so far. Tracked rather than left to the stream because seeing
  // data appended after an eof needs an explicit seek, not just a clear().
  std::streamoff offset_ = 0;
  bool drained_ = false;  // the partial tail has been handed out
};

// ---------------------------------------------------------------------------
// The network, which is what "phone GPS" means
// ---------------------------------------------------------------------------

// The shared socket plumbing: a non-blocking fd, one recv buffer and the
// framing. Not instantiated directly.
//
// WINDOWS IS GUARDED, NOT TESTED. The Winsock branch is written (WSAStartup is
// reference-counted per socket) and it compiles, but every test in this port
// runs on macOS, so treat a first Windows run as a bring-up.
class SocketLineTransport : public ILineTransport {
 public:
  ~SocketLineTransport() override;

  void Close() override;
  bool is_open() const override;
  LineResult ReadLine(std::string* out) override;

  // Bytes pulled off the socket since Open. A shell showing "no data" wants
  // this rather than a fix count: a stream of sentences the parser rejects
  // looks identical to silence otherwise.
  std::size_t bytes_received() const { return bytes_received_; }

 protected:
  SocketLineTransport() = default;

  // Reads whatever is ready into `buffer_`. Returns the ReadLine outcome to
  // report when no line came out of it.
  LineResult Pump();

  // Set by the subclass in Open().
  std::intptr_t fd_ = -1;
  bool datagram_ = false;

  static bool PlatformInit(Status* error);
  static void PlatformShutdown();
  static void CloseFd(std::intptr_t fd);
  static bool SetNonBlocking(std::intptr_t fd);

  LineBuffer buffer_;
  std::size_t bytes_received_ = 0;
  bool ended_ = false;
};

// A TCP client. GPS2IP, SharedGPS, gpsd's raw port and every other
// phone-as-a-receiver app is one of these: they listen, the map connects.
//
// Open() connects and RETURNS — a connect that is still in flight is not an
// error, and ReadLine answers kAgain until the handshake finishes. A far end
// that closes gives kEnd, which is a phone going to sleep and not a fault.
class TcpLineTransport : public SocketLineTransport {
 public:
  TcpLineTransport(std::string host, uint16_t port);

  Status Open() override;
  std::string description() const override;

  const std::string& host() const { return host_; }
  uint16_t port() const { return port_; }

 private:
  std::string host_;
  uint16_t port_;
};

// A UDP listener. The other half of "phone GPS": an app that BROADCASTS
// rather than serving. One datagram is normally one sentence, but the framing
// runs over datagrams too, so a sender that packs several per packet works and
// so does one that splits a sentence across two.
class UdpLineTransport : public SocketLineTransport {
 public:
  // `bind_host` empty binds every interface (0.0.0.0), which is what a
  // broadcast listener wants.
  explicit UdpLineTransport(uint16_t port, std::string bind_host = std::string());

  Status Open() override;
  std::string description() const override;

  uint16_t port() const { return port_; }

  // The port actually bound. Differs from `port()` only when 0 was asked for,
  // which is how a test gets an ephemeral port without racing another run.
  uint16_t bound_port() const { return bound_port_; }

 private:
  std::string bind_host_;
  uint16_t port_;
  uint16_t bound_port_ = 0;
};

}  // namespace fv

#endif  // FVKIT_NAV_LINE_TRANSPORT_H_
