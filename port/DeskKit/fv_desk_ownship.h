// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Chris Bailey
// Part of Peregrine, a cross-platform port of FalconView(tm).
// See COPYING.LESSER and NOTICE.md for the full licensing picture.

/// fv_desk_ownship.h — the Ownship window's model: the window's ownship
/// (`fv::OwnshipState`) and replay clock, formatted.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fvkit/nav/ownship.h"
#include "fvkit/nav/track_replay.h"

namespace fv {
namespace desk {

/// Seconds since 1970-01-01 UTC, now.
double UtcNowSeconds();

/// The units the Ownship window reads altitude and speed in (`ownship.units`).
enum class OwnshipUnits { kNautical, kMetric };  ///< ft and kt; m and km/h

/// `ownship.units` setting values, in `OwnshipUnits` order.
const std::vector<std::string>& OwnshipUnitNames();

/// The playback speeds the Ownship window offers, slowest first.
const std::vector<double>& PlaybackRates();

/// "10×" for 10; whole rates print without a fraction.
std::string PlaybackRateLabel(double rate);

/// The Ownship window's contents, formatted. A read-out that is not known is "—".
struct OwnshipPanel {
  OwnshipMode mode = OwnshipMode::kNone;
  std::string status;  ///< "No moving map", "Live" or "Replay"
  std::string source;  ///< the ownship map's name
  std::string time;
  std::string position;
  std::string altitude;
  std::string heading;
  bool heading_derived = false;  ///< the heading is the bearing of movement, not reported
  std::string speed;

  /// The time row: enabled while a replay map is open, whatever the ownship is.
  bool clock_enabled = false;
  double clock_s = 0.0;  ///< seconds since 1970-01-01 UTC
  double start_s = 0.0;  ///< the slider's range: every open replay map
  double end_s = 0.0;
  std::string clock_text;  ///< the clock as a UTC time of day, "" when disabled
  bool playing = false;
  double rate = 1.0;
  int rate_index = -1;  ///< into `PlaybackRates()`; -1 when the rate is not one of them

  uint64_t generation = 0;  ///< `Desk::OwnshipPanelGeneration` when built
};

/// Formats `state` and `clock` for the Ownship window. `has_range` is false
/// with no replay map open.
OwnshipPanel BuildOwnshipPanel(const OwnshipState& state, const ReplayClock& clock,
                               bool has_range, double start_s, double end_s,
                               OwnshipUnits units);

}  // namespace desk
}  // namespace fv
