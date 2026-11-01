// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// @file
/// A local AIS receiver or boat network: NMEA 0183 over TCP or UDP, split into
/// AIS sentences for the `fv.ais` overlay and position fixes for the moving
/// map. Owns the connection and its reconnect schedule; time comes from the
/// caller, so it runs on whatever thread polls it. Plan: port/ais-plan.md (AIS5).

#ifndef FVKIT_AIS_RECEIVER_H_
#define FVKIT_AIS_RECEIVER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/ais/decoder.h"
#include "fvkit/nav/line_transport.h"
#include "fvkit/nav/nmea.h"
#include "fvkit/nav/position.h"

namespace fv {
namespace ais {

enum class ReceiverProtocol { kTcp, kUdp };

/// Where the receiver is. TCP connects to `host`:`port`; UDP listens on `port`
/// on every interface (`host` unused).
struct ReceiverConfig {
  ReceiverProtocol protocol = ReceiverProtocol::kTcp;
  std::string host;
  uint16_t port = 10110;
};

enum class ReceiverState {
  kOff,
  kConnecting,  ///< open, nothing heard yet
  kLive,        ///< a line within `silence_s`
  kSilent,      ///< open, but nothing for `silence_s`
  kRetrying,    ///< closed after a failure; reopens at `retry_at_s()`
};

const char* ReceiverStateName(ReceiverState state);

/// What one `Poll` read.
struct ReceiverBatch {
  std::vector<std::string> ais_lines;  ///< `!xxVDM`/`!xxVDO`, TAG block kept
  /// From RMC/GGA/GLL/VTG with HDT attached, or from the boat's own `!xxVDO`
  /// position report while no GPS sentence has given a fix for
  /// `kVdoFallbackAfterS`.
  std::vector<PositionFix> fixes;
  bool state_changed = false;
};

class NmeaReceiver {
 public:
  using TransportFactory = std::function<std::shared_ptr<ILineTransport>()>;

  /// TCP client or UDP listener for `config`.
  static TransportFactory FactoryFor(const ReceiverConfig& config);

  explicit NmeaReceiver(TransportFactory factory);

  /// Opens the connection; a failure schedules a retry rather than stopping.
  void Start(double now_s);
  /// Closes the connection and returns to `kOff`.
  void Stop();

  /// Reads up to `max_lines`, reopening first when a retry is due. A TCP
  /// stream that ends, fails, or stays silent for `dead_s` is closed and
  /// retried with backoff (2 s doubling to 60 s, reset by a line).
  void Poll(double now_s, ReceiverBatch* out, std::size_t max_lines = 2000);

  ReceiverState state() const { return state_; }
  /// Why the last connection closed; empty until one has.
  const std::string& last_error() const { return last_error_; }
  double retry_at_s() const { return retry_at_s_; }
  std::string description() const;

  std::size_t lines_received() const { return lines_received_; }
  std::size_t ais_lines() const { return ais_lines_; }
  std::size_t fixes_emitted() const { return fixes_emitted_; }
  /// Of `fixes_emitted`, those made from `!xxVDO`.
  std::size_t vdo_fixes() const { return vdo_fixes_; }
  /// Lines that were neither AIS nor a position the assembler accepted.
  std::size_t lines_ignored() const { return lines_ignored_; }
  /// Time of the last line; 0 before the first.
  double last_line_s() const { return last_line_s_; }

  /// Live becomes silent after this long without a line (default 15 s).
  void set_silence_s(double s) { silence_s_ = s; }
  /// A silent TCP connection is closed and retried after this (default 60 s).
  void set_dead_s(double s) { dead_s_ = s; }

  /// A fix slower than this takes the HDT heading in place of its course,
  /// since course over ground is noise at rest (0.5 m/s, about 1 knot).
  static constexpr double kHeadingSpeedMps = 0.5;
  /// An HDT older than this is not attached to a fix.
  static constexpr double kHeadingMaxAgeS = 5.0;
  /// VDO positions become fixes only after this long without a GPS-sentence fix.
  static constexpr double kVdoFallbackAfterS = 10.0;

 private:
  void Open(double now_s);
  void Fail(double now_s, const std::string& reason);
  void SetState(ReceiverState state, ReceiverBatch* out);
  void TakeLine(const std::string& line, double now_s, ReceiverBatch* out);
  void Emit(PositionFix fix, double now_s, ReceiverBatch* out);
  void TakeOwnShip(const std::string& line, double now_s, ReceiverBatch* out);

  TransportFactory factory_;
  std::shared_ptr<ILineTransport> transport_;
  bool datagram_ = false;
  NmeaFixAssembler assembler_;
  AisDecoder vdo_decoder_;
  bool has_gps_fix_ = false;
  double last_gps_fix_s_ = 0.0;
  ReceiverState state_ = ReceiverState::kOff;
  ReceiverState reported_ = ReceiverState::kOff;
  std::string last_error_;
  double retry_at_s_ = 0.0;
  double retry_delay_s_ = 2.0;
  double opened_s_ = 0.0;
  double last_line_s_ = 0.0;
  double silence_s_ = 15.0;
  double dead_s_ = 60.0;
  bool has_heading_ = false;
  double heading_deg_ = 0.0;
  double heading_s_ = 0.0;
  std::size_t lines_received_ = 0;
  std::size_t ais_lines_ = 0;
  std::size_t fixes_emitted_ = 0;
  std::size_t lines_ignored_ = 0;
  std::size_t vdo_fixes_ = 0;
};

/// True for an AIS sentence (`!xxVDM`, `!xxVDO`), with or without a TAG block.
bool IsAisSentence(const std::string& line);

}  // namespace ais
}  // namespace fv

#endif  // FVKIT_AIS_RECEIVER_H_
