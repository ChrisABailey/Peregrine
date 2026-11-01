// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

#include "fvkit/ais/receiver.h"

#include <algorithm>
#include <utility>

namespace fv {
namespace ais {

namespace {

constexpr double kFirstRetryS = 2.0;
constexpr double kMaxRetryS = 60.0;

}  // namespace

const char* ReceiverStateName(ReceiverState state) {
  switch (state) {
    case ReceiverState::kOff: return "off";
    case ReceiverState::kConnecting: return "connecting";
    case ReceiverState::kLive: return "live";
    case ReceiverState::kSilent: return "silent";
    case ReceiverState::kRetrying: return "retrying";
  }
  return "off";
}

bool IsAisSentence(const std::string& line) {
  std::size_t start = 0;
  // A TAG block is "\...*hh\" before the sentence.
  if (!line.empty() && line[0] == '\\') {
    const std::size_t close = line.find('\\', 1);
    if (close == std::string::npos) return false;
    start = close + 1;
  }
  if (line.size() < start + 7 || line[start] != '!') return false;
  return line.compare(start + 3, 4, "VDM,") == 0 || line.compare(start + 3, 4, "VDO,") == 0;
}

NmeaReceiver::TransportFactory NmeaReceiver::FactoryFor(const ReceiverConfig& config) {
  return [config]() -> std::shared_ptr<ILineTransport> {
    if (config.protocol == ReceiverProtocol::kUdp) {
      return std::make_shared<UdpLineTransport>(config.port);
    }
    return std::make_shared<TcpLineTransport>(config.host, config.port);
  };
}

NmeaReceiver::NmeaReceiver(TransportFactory factory) : factory_(std::move(factory)) {}

std::string NmeaReceiver::description() const {
  if (transport_) return transport_->description();
  const std::shared_ptr<ILineTransport> probe = factory_ ? factory_() : nullptr;
  return probe ? probe->description() : std::string();
}

void NmeaReceiver::Start(double now_s) {
  Stop();
  int year = 0, month = 0, day = 0;
  double seconds = 0.0;
  // A GGA-only receiver never states the date; today's UTC date stamps it.
  EpochSecondsToUtc(now_s, &year, &month, &day, &seconds);
  assembler_.SetDateHint(year, month, day);
  retry_delay_s_ = kFirstRetryS;
  last_error_.clear();
  Open(now_s);
}

void NmeaReceiver::Stop() {
  if (transport_) transport_->Close();
  transport_.reset();
  assembler_.Reset();
  vdo_decoder_ = AisDecoder();
  has_heading_ = false;
  has_gps_fix_ = false;
  state_ = ReceiverState::kOff;
  reported_ = ReceiverState::kOff;
}

void NmeaReceiver::Open(double now_s) {
  transport_ = factory_ ? factory_() : nullptr;
  if (!transport_) {
    Fail(now_s, "no transport");
    return;
  }
  datagram_ = dynamic_cast<UdpLineTransport*>(transport_.get()) != nullptr;
  const Status opened = transport_->Open();
  if (!opened.ok()) {
    Fail(now_s, opened.message);
    return;
  }
  opened_s_ = now_s;
  state_ = ReceiverState::kConnecting;
}

void NmeaReceiver::Fail(double now_s, const std::string& reason) {
  if (transport_) transport_->Close();
  transport_.reset();
  assembler_.Reset();
  last_error_ = reason;
  state_ = ReceiverState::kRetrying;
  retry_at_s_ = now_s + retry_delay_s_;
  retry_delay_s_ = std::min(retry_delay_s_ * 2.0, kMaxRetryS);
}

void NmeaReceiver::SetState(ReceiverState state, ReceiverBatch* out) {
  state_ = state;
  if (state_ != reported_) {
    reported_ = state_;
    out->state_changed = true;
  }
}

void NmeaReceiver::Poll(double now_s, ReceiverBatch* out, std::size_t max_lines) {
  if (state_ == ReceiverState::kOff) {
    SetState(ReceiverState::kOff, out);
    return;
  }
  if (state_ == ReceiverState::kRetrying && now_s >= retry_at_s_) Open(now_s);
  if (state_ == ReceiverState::kRetrying) {
    SetState(ReceiverState::kRetrying, out);
    return;
  }

  std::string line;
  for (std::size_t n = 0; n < max_lines; ++n) {
    const LineResult r = transport_->ReadLine(&line);
    if (r == LineResult::kAgain) break;
    if (r == LineResult::kLine) {
      ++lines_received_;
      last_line_s_ = now_s;
      retry_delay_s_ = kFirstRetryS;
      state_ = ReceiverState::kLive;
      TakeLine(line, now_s, out);
      continue;
    }
    PositionFix fix;
    if (assembler_.Flush(&fix)) {
      has_gps_fix_ = true;
      last_gps_fix_s_ = now_s;
      Emit(fix, now_s, out);
    }
    Fail(now_s, r == LineResult::kEnd ? std::string("connection closed by the receiver")
                                      : transport_->error().message);
    SetState(ReceiverState::kRetrying, out);
    return;
  }

  const double heard_s = std::max(last_line_s_, opened_s_);
  if (!datagram_ && now_s - heard_s > dead_s_) {
    Fail(now_s, "no data");
  } else if (state_ != ReceiverState::kConnecting && now_s - heard_s > silence_s_) {
    state_ = ReceiverState::kSilent;
  }
  SetState(state_, out);
}

void NmeaReceiver::TakeLine(const std::string& line, double now_s, ReceiverBatch* out) {
  if (IsAisSentence(line)) {
    ++ais_lines_;
    out->ais_lines.push_back(line);
    TakeOwnShip(line, now_s, out);
    return;
  }
  double heading = 0.0;
  if (ParseNmeaTrueHeading(line, &heading)) {
    has_heading_ = true;
    heading_deg_ = heading;
    heading_s_ = now_s;
    return;
  }
  const std::size_t parsed = assembler_.sentences_parsed();
  PositionFix fix;
  if (assembler_.AddLine(line, &fix)) {
    has_gps_fix_ = true;
    last_gps_fix_s_ = now_s;
    Emit(fix, now_s, out);
  }
  if (assembler_.sentences_parsed() == parsed) ++lines_ignored_;
}

void NmeaReceiver::TakeOwnShip(const std::string& line, double now_s, ReceiverBatch* out) {
  // Only VDO lines go through this decoder; the overlay decodes every line itself.
  const std::size_t bang = line.find('!');
  if (bang == std::string::npos || line.compare(bang + 3, 3, "VDO") != 0) return;
  AisMessage m;
  if (vdo_decoder_.Feed(line, now_s, &m) != FeedResult::kMessage) return;
  // Position reports at full resolution; type 27 (1/10 minute) is too coarse.
  const bool report = (m.type >= 1 && m.type <= 3) || m.type == 18 || m.type == 19;
  if (!report || !m.has_position) return;
  if (has_gps_fix_ && now_s - last_gps_fix_s_ < kVdoFallbackAfterS) return;

  PositionFix fix;
  fix.SetPosition(m.lat_deg, m.lon_deg);
  if (m.has_sog) {
    fix.speed_mps = m.sog_mps;
    fix.has_speed = true;
  }
  if (m.has_cog) {
    fix.true_heading_deg = m.cog_deg;
    fix.has_true_heading = true;
  }
  // The transponder's heading stands in for course at rest, as HDT does.
  const bool slow = fix.has_speed && fix.speed_mps < kHeadingSpeedMps;
  if (m.has_heading && (!fix.has_true_heading || slow)) {
    fix.true_heading_deg = m.heading_deg;
    fix.has_true_heading = true;
  }
  // AIS carries only the second of the minute; the fix is stamped on arrival.
  fix.time_s = now_s;
  fix.has_time = true;
  ++vdo_fixes_;
  Emit(fix, now_s, out);
}

void NmeaReceiver::Emit(PositionFix fix, double now_s, ReceiverBatch* out) {
  const bool slow = fix.has_speed && fix.speed_mps < kHeadingSpeedMps;
  if (has_heading_ && now_s - heading_s_ <= kHeadingMaxAgeS && (!fix.has_true_heading || slow)) {
    fix.true_heading_deg = heading_deg_;
    fix.has_true_heading = true;
  }
  ++fixes_emitted_;
  out->fixes.push_back(fix);
}

}  // namespace ais
}  // namespace fv
