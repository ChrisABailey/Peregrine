// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See LICENSE and NOTICE.md for the full licensing picture.

#include "fvkit/nav/line_transport.h"

#include <cstring>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fv {
namespace {

// One recv per Pump. 4 KB is ~50 sentences, so a 1 Hz feed is never behind and
// a firehose is still read in bounded chunks.
constexpr std::size_t kRecvChunk = 4096;

// The one place the two socket APIs differ in TYPE rather than in behaviour.
#ifdef _WIN32
using SockHandle = SOCKET;
using SockLen = int;
inline const char* OptPtr(const int* value) { return reinterpret_cast<const char*>(value); }
inline int LastSocketError() { return WSAGetLastError(); }
inline bool ErrWouldBlock(int e) { return e == WSAEWOULDBLOCK; }
inline bool ErrInterrupted(int /*e*/) { return false; }
inline bool ErrConnectInFlight(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline std::string SocketErrorText(int e) { return std::to_string(e); }
#else
using SockHandle = int;
using SockLen = socklen_t;
inline const int* OptPtr(const int* value) { return value; }
inline int LastSocketError() { return errno; }
inline bool ErrWouldBlock(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
inline bool ErrInterrupted(int e) { return e == EINTR; }
inline bool ErrConnectInFlight(int e) { return e == EINPROGRESS || e == EALREADY; }
inline std::string SocketErrorText(int e) { return std::strerror(e); }
#endif

inline SockHandle Sock(std::intptr_t fd) { return static_cast<SockHandle>(fd); }

}  // namespace

// ---------------------------------------------------------------------------
// LineBuffer
// ---------------------------------------------------------------------------

void LineBuffer::Compact() {
  if (consumed_ == 0) return;
  buffer_.erase(0, consumed_);
  consumed_ = 0;
}

void LineBuffer::Clear() {
  buffer_.clear();
  consumed_ = 0;
  discarding_ = false;
}

void LineBuffer::Append(const char* data, std::size_t length) {
  if (data == nullptr || length == 0) return;
  Compact();
  buffer_.append(data, length);
}

bool LineBuffer::NextLine(std::string* out) {
  for (;;) {
    const std::size_t nl = buffer_.find('\n', consumed_);
    if (nl == std::string::npos) {
      // No terminator yet. What is pending already exceeds the cap, so it can
      // never become a legal line: count it once and skip to the next '\n'
      // rather than letting the buffer grow with it.
      if (!discarding_ && pending_bytes() > max_line_length_) {
        discarding_ = true;
        ++overlong_dropped_;
        consumed_ = buffer_.size();
        Compact();
      }
      return false;
    }

    const std::size_t begin = consumed_;
    std::size_t end = nl;
    consumed_ = nl + 1;
    if (discarding_) {
      discarding_ = false;  // the tail of a line already counted as dropped
      continue;
    }
    if (end > begin && buffer_[end - 1] == '\r') --end;
    const std::size_t length = end - begin;
    if (length == 0) continue;  // a blank line is never a sentence
    if (length > max_line_length_) {
      ++overlong_dropped_;
      continue;
    }
    if (out != nullptr) out->assign(buffer_, begin, length);
    return true;
  }
}

bool LineBuffer::TakePartial(std::string* out) {
  if (discarding_) {
    // The unterminated tail of a line already counted as dropped.
    Clear();
    return false;
  }
  const std::size_t begin = consumed_;
  std::size_t end = buffer_.size();
  if (end > begin && buffer_[end - 1] == '\r') --end;
  const std::size_t length = end > begin ? end - begin : 0;
  if (length == 0 || length > max_line_length_) {
    if (length > max_line_length_) ++overlong_dropped_;
    Clear();
    return false;
  }
  if (out != nullptr) out->assign(buffer_, begin, length);
  Clear();
  return true;
}

// ---------------------------------------------------------------------------
// StringLineTransport
// ---------------------------------------------------------------------------

StringLineTransport::StringLineTransport(std::string data, bool auto_end) : auto_end_(auto_end) {
  buffer_.Append(data);
}

void StringLineTransport::AddData(const std::string& data) { buffer_.Append(data); }

void StringLineTransport::SetEnded() { ended_ = true; }

Status StringLineTransport::Open() {
  open_ = true;
  return Status::Ok();
}

void StringLineTransport::Close() { open_ = false; }

LineResult StringLineTransport::ReadLine(std::string* out) {
  if (!open_) return LineResult::kEnd;
  if (buffer_.NextLine(out)) return LineResult::kLine;
  if (ended_ || auto_end_) {
    if (buffer_.TakePartial(out)) return LineResult::kLine;
    return LineResult::kEnd;
  }
  return LineResult::kAgain;
}

// ---------------------------------------------------------------------------
// FileLineTransport
// ---------------------------------------------------------------------------

FileLineTransport::FileLineTransport(std::string path, bool follow)
    : path_(std::move(path)), follow_(follow) {}

Status FileLineTransport::Open() {
  if (stream_.is_open()) return Status::Ok();
  buffer_.Clear();
  drained_ = false;
  offset_ = 0;
  stream_.open(path_, std::ios::binary);
  if (!stream_.is_open()) {
    error_ = Status::Error(kNotFound, "FileLineTransport: cannot open " + path_);
    return error_;
  }
  return Status::Ok();
}

void FileLineTransport::Close() {
  stream_.close();
  buffer_.Clear();
  drained_ = false;
  offset_ = 0;
}

LineResult FileLineTransport::ReadLine(std::string* out) {
  if (!stream_.is_open()) return LineResult::kEnd;
  for (;;) {
    if (buffer_.NextLine(out)) return LineResult::kLine;

    char chunk[kRecvChunk];
    // Clearing eof is not enough to see bytes appended since: the stream's
    // buffer has to be re-primed, and only a seek does that portably. So the
    // read offset is tracked here and sought to explicitly — without it
    // `follow` reads to the end of the file once and never resumes.
    stream_.clear();
    stream_.seekg(offset_);
    stream_.read(chunk, static_cast<std::streamsize>(sizeof(chunk)));
    const std::streamsize got = stream_.gcount();
    if (got > 0) {
      offset_ += got;
      buffer_.Append(chunk, static_cast<std::size_t>(got));
      continue;
    }

    // Nothing more on disk right now. A recording whose last line has no
    // newline still yields its last sentence, exactly once — but only when
    // the file has ENDED. While following, an unterminated tail is a line
    // still being written, and handing it over would deliver its first half
    // and then its second half again.
    if (!follow_) {
      if (!drained_ && buffer_.TakePartial(out)) {
        drained_ = true;
        return LineResult::kLine;
      }
      drained_ = true;
      return LineResult::kEnd;
    }
    return LineResult::kAgain;
  }
}

// ---------------------------------------------------------------------------
// SocketLineTransport
// ---------------------------------------------------------------------------

#ifdef _WIN32
namespace {
int g_wsa_refs = 0;
}  // namespace

bool SocketLineTransport::PlatformInit(Status* error) {
  if (g_wsa_refs++ > 0) return true;
  WSADATA data;
  const int rc = WSAStartup(MAKEWORD(2, 2), &data);
  if (rc != 0) {
    --g_wsa_refs;
    if (error != nullptr) {
      *error = Status::Error(kIoError, "WSAStartup failed: " + std::to_string(rc));
    }
    return false;
  }
  return true;
}

void SocketLineTransport::PlatformShutdown() {
  if (g_wsa_refs > 0 && --g_wsa_refs == 0) WSACleanup();
}

void SocketLineTransport::CloseFd(std::intptr_t fd) {
  if (fd >= 0) ::closesocket(Sock(fd));
}

bool SocketLineTransport::SetNonBlocking(std::intptr_t fd) {
  u_long mode = 1;
  return ::ioctlsocket(Sock(fd), FIONBIO, &mode) == 0;
}
#else
bool SocketLineTransport::PlatformInit(Status* /*error*/) { return true; }

void SocketLineTransport::PlatformShutdown() {}

void SocketLineTransport::CloseFd(std::intptr_t fd) {
  if (fd >= 0) ::close(Sock(fd));
}

bool SocketLineTransport::SetNonBlocking(std::intptr_t fd) {
  const int flags = ::fcntl(Sock(fd), F_GETFL, 0);
  if (flags < 0) return false;
  return ::fcntl(Sock(fd), F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

SocketLineTransport::~SocketLineTransport() { Close(); }

bool SocketLineTransport::is_open() const { return fd_ >= 0; }

void SocketLineTransport::Close() {
  if (fd_ >= 0) {
    CloseFd(fd_);
    fd_ = -1;
    PlatformShutdown();
  }
  buffer_.Clear();
  bytes_received_ = 0;
  ended_ = false;
}

LineResult SocketLineTransport::Pump() {
  char chunk[kRecvChunk];
  const auto got = ::recv(Sock(fd_), chunk, static_cast<int>(sizeof(chunk)), 0);
  if (got > 0) {
    bytes_received_ += static_cast<std::size_t>(got);
    buffer_.Append(chunk, static_cast<std::size_t>(got));
    return LineResult::kLine;  // bytes arrived; the caller re-checks the buffer
  }
  if (got == 0) {
    // A stream socket's far end closed. A datagram socket does not say this
    // (a zero-length datagram is legal and means nothing), so it stays live.
    if (!datagram_) {
      ended_ = true;
      return LineResult::kEnd;
    }
    return LineResult::kAgain;
  }
  const int err = LastSocketError();
  if (ErrWouldBlock(err) || ErrInterrupted(err)) return LineResult::kAgain;
  error_ = Status::Error(kIoError, "recv failed: " + SocketErrorText(err));
  return LineResult::kError;
}

LineResult SocketLineTransport::ReadLine(std::string* out) {
  if (fd_ < 0) return LineResult::kEnd;
  for (;;) {
    if (buffer_.NextLine(out)) return LineResult::kLine;
    if (ended_) {
      if (buffer_.TakePartial(out)) return LineResult::kLine;
      return LineResult::kEnd;
    }
    const LineResult pumped = Pump();
    if (pumped == LineResult::kLine) continue;  // look again
    if (pumped == LineResult::kEnd) {
      if (buffer_.TakePartial(out)) return LineResult::kLine;
      return LineResult::kEnd;
    }
    return pumped;  // kAgain or kError
  }
}

// ---------------------------------------------------------------------------
// TcpLineTransport
// ---------------------------------------------------------------------------

TcpLineTransport::TcpLineTransport(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) {}

std::string TcpLineTransport::description() const {
  return "tcp:" + host_ + ":" + std::to_string(port_);
}

Status TcpLineTransport::Open() {
  if (fd_ >= 0) return Status::Ok();
  datagram_ = false;
  if (!PlatformInit(&error_)) return error_;

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;  // a phone on a v6-only link is still a phone
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const std::string service = std::to_string(port_);
  if (::getaddrinfo(host_.c_str(), service.c_str(), &hints, &results) != 0 || results == nullptr) {
    PlatformShutdown();
    error_ = Status::Error(kNotFound, "TcpLineTransport: cannot resolve " + host_);
    return error_;
  }

  for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
    const std::intptr_t fd =
        static_cast<std::intptr_t>(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
    if (fd < 0) continue;
    // Non-blocking BEFORE connect: a phone that is asleep would otherwise hang
    // the shell's tick for the whole TCP timeout.
    if (!SetNonBlocking(fd)) {
      CloseFd(fd);
      continue;
    }
    const int rc = ::connect(Sock(fd), ai->ai_addr, static_cast<SockLen>(ai->ai_addrlen));
    if (rc == 0 || ErrConnectInFlight(LastSocketError())) {
      fd_ = fd;
      buffer_.Clear();
      bytes_received_ = 0;
      ended_ = false;
      ::freeaddrinfo(results);
      return Status::Ok();
    }
    CloseFd(fd);
  }
  ::freeaddrinfo(results);
  PlatformShutdown();
  error_ = Status::Error(kIoError, "TcpLineTransport: cannot connect to " + description());
  return error_;
}

// ---------------------------------------------------------------------------
// UdpLineTransport
// ---------------------------------------------------------------------------

UdpLineTransport::UdpLineTransport(uint16_t port, std::string bind_host)
    : bind_host_(std::move(bind_host)), port_(port) {}

std::string UdpLineTransport::description() const {
  const std::string host = bind_host_.empty() ? std::string("*") : bind_host_;
  return "udp:" + host + ":" + std::to_string(bound_port_ != 0 ? bound_port_ : port_);
}

Status UdpLineTransport::Open() {
  if (fd_ >= 0) return Status::Ok();
  datagram_ = true;
  if (!PlatformInit(&error_)) return error_;

  const std::intptr_t fd = static_cast<std::intptr_t>(::socket(AF_INET, SOCK_DGRAM, 0));
  if (fd < 0) {
    PlatformShutdown();
    error_ = Status::Error(kIoError, "UdpLineTransport: socket failed");
    return error_;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  if (bind_host_.empty()) {
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (::inet_pton(AF_INET, bind_host_.c_str(), &addr.sin_addr) != 1) {
    CloseFd(fd);
    PlatformShutdown();
    error_ = Status::Error(kInvalidArg, "UdpLineTransport: bad bind address " + bind_host_);
    return error_;
  }

  const int reuse = 1;
  ::setsockopt(Sock(fd), SOL_SOCKET, SO_REUSEADDR, OptPtr(&reuse), sizeof(reuse));

  if (::bind(Sock(fd), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    const int err = LastSocketError();
    CloseFd(fd);
    PlatformShutdown();
    error_ = Status::Error(kIoError, "UdpLineTransport: cannot bind port " +
                                        std::to_string(port_) + ": " + SocketErrorText(err));
    return error_;
  }
  if (!SetNonBlocking(fd)) {
    CloseFd(fd);
    PlatformShutdown();
    error_ = Status::Error(kIoError, "UdpLineTransport: cannot set non-blocking");
    return error_;
  }

  // Report the port actually bound, so a caller that asked for 0 can tell a
  // sender where to aim.
  sockaddr_in actual{};
  SockLen actual_len = sizeof(actual);
  if (::getsockname(Sock(fd), reinterpret_cast<sockaddr*>(&actual), &actual_len) == 0) {
    bound_port_ = ntohs(actual.sin_port);
  } else {
    bound_port_ = port_;
  }

  fd_ = fd;
  buffer_.Clear();
  bytes_received_ = 0;
  ended_ = false;
  return Status::Ok();
}

}  // namespace fv
